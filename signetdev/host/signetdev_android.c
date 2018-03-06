#include "signetdev_priv.h"
#include "signetdev_unix.h"
#include "signetdev.h"

#include <poll.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#include <linux/ioctl.h>
#include <errno.h>
#include <stdlib.h>

extern signetdev_conn_err_t g_error_handler;
extern void *g_error_handler_param;

struct signetdev_connection {
	int fd;
	int tx_endpoint;
	int rx_endpoint;
	int has_keyboard;
	struct send_message_req *tail_message;
	struct send_message_req *head_message;
	struct send_message_req *tail_cancel_message;
	struct send_message_req *head_cancel_message;

	struct tx_message_state tx_state;
	struct rx_message_state rx_state;
	struct usbdevfs_urb read_urb;
	int write_pending;
	struct usbdevfs_urb write_urb;
	u8 rx_packet_buf[RAW_HID_PACKET_SIZE];
};

struct signetdev_connection g_connection;

static struct send_message_req **pending_message()
{
	struct signetdev_connection *conn = &g_connection;
	struct send_message_req **msg = NULL;
	if (conn->rx_state.message) {
		msg = &conn->rx_state.message;
	} else if (conn->tx_state.message && !conn->tx_state.message->interrupt) {
		msg = &conn->tx_state.message;
	}
	return msg;
}

void signetdev_priv_handle_error()
{
	struct signetdev_connection *conn = &g_connection;
	if (conn->fd != -1) {
		conn->fd  = -1;
	}
	if (g_error_handler) {
		g_error_handler(g_error_handler_param);
	}
}

int signetdev_priv_issue_command(int command, void *p)
{
	intptr_t v[2] = {command, (intptr_t)p};
	write(g_command_pipe[1], v, sizeof(intptr_t) * 2);
	char cmd_resp;
	read(g_command_resp_pipe[0], &cmd_resp, 1);
	return cmd_resp;
}

void signetdev_priv_issue_command_no_resp(int command, void *p)
{
	intptr_t v[2] = {command, (intptr_t)p};
	write(g_command_pipe[1], v, sizeof(intptr_t) * 2);
}

static void handle_exit(void *arg)
{
	(void)arg;
	struct send_message_req **msg = pending_message();
	if (msg)
		signetdev_priv_finalize_message(msg, SIGNET_ERROR_QUIT);
}

static int g_opening_connection = 0;

static void command_response(int rc)
{
	char resp = rc;
	write(g_command_resp_pipe[1], &resp, 1);
}

static void handle_command(int command, void *p)
{
	struct signetdev_connection *conn = &g_connection;
	switch (command) {
	case SIGNETDEV_CMD_OPEN:
		if (conn->fd != -1) {
			command_response(0);
		} else {
			g_opening_connection = 1;
			command_response(1);
		}
		break;
	case SIGNETDEV_CMD_CANCEL_OPEN:
		if (g_opening_connection) {
			g_opening_connection = 0;
			command_response(-1);
		}
		break;
	case SIGNETDEV_CMD_CLOSE:
		if (conn->fd != -1) {
			conn->fd = -1;
		}
		break;
	case SIGNETDEV_CMD_HAS_KEYBOARD: {
		command_response(conn->has_keyboard);
	} break;
	case SIGNETDEV_CMD_FD_ATTACHED: {
		struct attach_message *m = (struct attach_message *)p;
		if (conn->fd == -1) {
			conn->fd = m->fd;
			conn->rx_endpoint = m->rx_endpoint;
			conn->tx_endpoint = m->tx_endpoint;
			conn->has_keyboard = m->has_keyboard;
			memset(&conn->read_urb, 0, sizeof(struct usbdevfs_urb));
			conn->read_urb.buffer = conn->rx_packet_buf;
			conn->read_urb.buffer_length = RAW_HID_PACKET_SIZE;
			conn->read_urb.endpoint = conn->rx_endpoint | 0x80;
			conn->read_urb.type = USBDEVFS_URB_TYPE_INTERRUPT;
			int rc = ioctl(conn->fd, USBDEVFS_SUBMITURB, &conn->read_urb);
			if (rc == -1) {
				signetdev_priv_handle_error();
			} else if (g_opening_connection) {
				g_opening_connection = 0;
				g_device_opened_cb(g_device_opened_cb_param);
			}
		}
		free(m);
		} break;
	case SIGNETDEV_CMD_FD_DETACHED: {
		int fd = (intptr_t) p;
		if (fd == conn->fd) {
			signetdev_priv_handle_error();
		}
		} break;
	case SIGNETDEV_CMD_QUIT:
		handle_exit(NULL);
		pthread_exit(NULL);
		break;
	case SIGNETDEV_CMD_RESET_CONNECTION:
		if (conn->fd != -1) {
			ioctl(conn->fd, USBDEVFS_RESET, 0);
		}
		break;
	case SIGNETDEV_CMD_MESSAGE: {
		struct signetdev_connection *conn = &g_connection;
		struct send_message_req *msg = (struct send_message_req *)p;
		msg->next = NULL;
		if (!conn->head_message) {
			conn->head_message = msg;
		}
		if (conn->tail_message)
			conn->tail_message->next = msg;
		conn->tail_message = msg;
		} break;
	case SIGNETDEV_CMD_CANCEL_MESSAGE: {
		struct send_message_req *msg = (struct send_message_req *)p;
		msg->next = NULL;
		if (!conn->head_cancel_message) {
			conn->head_cancel_message = msg;
		}
		if (conn->tail_cancel_message)
			conn->tail_cancel_message->next = msg;
		conn->tail_cancel_message = msg;
		} break;
	}

}

static void command_pipe_io_iter()
{
	int done = 0;
	while (!done) {
		intptr_t v[2];
		int rc = read(g_command_pipe[0], v, sizeof(intptr_t) * 2);
		if (rc == sizeof(intptr_t) * 2) {
			handle_command(v[0], (void *)v[1]);
		} else if (rc == -1 && errno == EAGAIN) {
			done = 1;
		} else {
			signetdev_priv_handle_error();
		}
	}
}

static void usbdevfs_submit_tx_urb(struct signetdev_connection *conn)
{
	if (!conn->write_pending) {
		memset(&conn->write_urb, 0, sizeof(struct usbdevfs_urb));
		conn->write_urb.buffer = conn->tx_state.packet_buf + 1;
		conn->write_urb.buffer_length = RAW_HID_PACKET_SIZE;
		conn->write_urb.endpoint = conn->tx_endpoint;
		conn->write_urb.type = USBDEVFS_URB_TYPE_INTERRUPT;
		int rc = ioctl(conn->fd, USBDEVFS_SUBMITURB, &conn->write_urb);
		if (rc == -1) {
			signetdev_priv_handle_error();
			conn->write_pending = 0;
			return;
		} else {
			conn->write_pending = 1;
		}
	}
}

void signetdev_reset_connection()
{
	signetdev_priv_issue_command_no_resp(SIGNETDEV_CMD_RESET_CONNECTION, NULL);
}

static void usbdevfs_urb_complete(struct signetdev_connection *conn, struct usbdevfs_urb *urb)
{
	if (urb == &conn->read_urb) {
		if (urb->actual_length != RAW_HID_PACKET_SIZE) {
			signetdev_priv_handle_error();
			return;
		} else {
			signetdev_priv_process_rx_packet(&conn->rx_state, urb->buffer);
		}
		memset(&conn->read_urb, 0, sizeof(struct usbdevfs_urb));
		conn->read_urb.buffer = conn->rx_packet_buf;
		conn->read_urb.buffer_length = RAW_HID_PACKET_SIZE;
		conn->read_urb.endpoint = conn->rx_endpoint | 0x80;
		conn->read_urb.type = USBDEVFS_URB_TYPE_INTERRUPT;
		int rc = ioctl(conn->fd, USBDEVFS_SUBMITURB, &conn->read_urb);
		if (rc == -1) {
			signetdev_priv_handle_error();
			return;
		}
	} else if (urb == &conn->write_urb) {
		conn->write_pending = 0;
		if (conn->tx_state.msg_packet_seq == conn->tx_state.msg_packet_count) {
			if (!conn->tx_state.message->resp) {
				signetdev_priv_finalize_message(&conn->tx_state.message, conn->tx_state.msg_size);
			} else {
				conn->tx_state.message = NULL;
			}
			return;
		}
		signetdev_priv_advance_message_state(&conn->tx_state);
		usbdevfs_submit_tx_urb(conn);
	}
}


static void raw_hid_io(struct signetdev_connection *conn)
{
	if (!conn->tx_state.message && (conn->head_message || conn->head_cancel_message)) {
		if (conn->head_cancel_message) {
			conn->tx_state.message = conn->head_cancel_message;
			conn->head_cancel_message = conn->head_cancel_message->next;
			if (!conn->head_cancel_message) {
				conn->tail_cancel_message = NULL;
			}
		} else if (!conn->rx_state.message) {
			conn->tx_state.message = conn->head_message;
			if (conn->head_message->resp) {
				conn->rx_state.message = conn->head_message;
			}
			conn->head_message = conn->head_message->next;
			if (!conn->head_message) {
				conn->tail_message = NULL;
			}
		}
		if (conn->tx_state.message) {
			signetdev_priv_prepare_message_state(&conn->tx_state,
					 conn->tx_state.message->dev_cmd,
					 conn->tx_state.message->payload,
					 conn->tx_state.message->payload_size);
			signetdev_priv_advance_message_state(&conn->tx_state);
			usbdevfs_submit_tx_urb(conn);
		}
	}
}

static void usbdevfs_io_iter(struct signetdev_connection *conn)
{
	while (conn->fd != -1) {
		struct usbdevfs_urb *urb;
		int rc = ioctl(conn->fd, USBDEVFS_REAPURBNDELAY, &urb);
		if (rc == -1 && errno == EAGAIN) {
			raw_hid_io(conn);
			return;
		} else if (rc == -1) {
			signetdev_priv_handle_error();
			return;
		} else {
			usbdevfs_urb_complete(conn, urb);
		}
		if (conn->fd != -1) {
			raw_hid_io(conn);
		}
	}
}

void *transaction_thread(void *arg)
{
	(void)(arg);
	struct pollfd poll_fds[8];
	struct signetdev_connection *conn = &g_connection;
	conn->fd = -1;

	while (1) {
		int num_poll_fds = 1;
		poll_fds[0].fd = g_command_pipe[0];
		poll_fds[0].events = POLLIN;
		poll_fds[0].revents = 0;
		if (conn->fd > 0) {
			poll_fds[1].fd = conn->fd;
			poll_fds[1].events = POLLOUT;
			poll_fds[1].revents = 0;
			num_poll_fds++;
		}
		poll(poll_fds, num_poll_fds, -1);
		command_pipe_io_iter();
		usbdevfs_io_iter(conn);
	}
	return NULL;
}
