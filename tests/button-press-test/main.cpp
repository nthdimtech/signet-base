#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>

extern "C" {
#include "signetdev/host/signetdev.h"
}

int button_presses;

void deviceOpenedS(void *user)
{
	int token;
	button_presses = 0;
	printf("Device opened\n");
	::signetdev_startup(NULL, &token);
}

void deviceClosedS(void *user)
{
	int rc = ::signetdev_open_connection();
	if (rc == OKAY) {
		deviceOpenedS(NULL);
	}
}

void connectionErrorS(void *user)
{
	deviceClosedS(user);
}

void eventS(void *cb_param, int event_type, void *resp_data, int resp_len)
{
	button_presses++;
	if (event_type == 1) {
		printf(" %d", button_presses);
		if (button_presses == 10) {
			printf(" DONE\n");
			QCoreApplication::quit();
		}
		fflush(stdout);
	}
}

void signetCmdResponse(void *cb_param, void *cmd_user_param, int cmd_token, int cmd, int end_device_state, int messages_remaining, int resp_code, void *resp_data)
{
	switch(cmd) {
	case SIGNETDEV_CMD_STARTUP: {
		struct signetdev_startup_resp_data *data = (struct signetdev_startup_resp_data *)resp_data;
		printf("(FW ver %d.%d.%d)", data->fw_major_version, data->fw_minor_version, data->fw_step_version);
	} break;
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);
	int token;

	if (argc < 1)
		return 0;

	::signetdev_initialize_api();
	::signetdev_set_command_resp_cb(signetCmdResponse, NULL);
	::signetdev_set_device_opened_cb(deviceOpenedS, NULL);
	::signetdev_set_device_closed_cb(deviceClosedS, NULL);
	::signetdev_set_error_handler(connectionErrorS, NULL);
	::signetdev_set_device_event_cb(eventS, NULL);

	int rc = ::signetdev_open_connection();

	if (rc == OKAY) {
		deviceOpenedS(NULL);
	}
	return a.exec();
	::signetdev_deinitialize_api();
}
