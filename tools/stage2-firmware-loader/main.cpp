#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>

extern "C" {
#include "signetdev/host/signetdev.h"
}

struct fwSection {
	QString name;
	unsigned int lma;
	int size;
	QByteArray contents;
};

QList<fwSection> fwSections;
QList<fwSection>::iterator writingSectionIter;
unsigned int writingAddr;
unsigned int writingSize;

void deviceClosedS(void *user)
{
	printf("Device closed\n");
	QCoreApplication::quit();
}

void connectionErrorS(void *this_)
{
	printf("Programming complete\n");
	QCoreApplication::quit();
}

void sendFirmwareWriteCmd()
{
	int token;
	bool advance = false;
	unsigned int section_lma = writingSectionIter->lma;
	unsigned int section_size = writingSectionIter->size;
	unsigned int section_end = section_lma + section_size;
	unsigned int write_size = 1024;
	if ((writingAddr + write_size) >= section_end) {
		write_size = section_end - writingAddr;
		advance = true;
	}
	void *data = writingSectionIter->contents.data() + (writingAddr - section_lma);

	::signetdev_write_flash(NULL, &token, writingAddr, data, write_size);
	if (advance) {
		writingSectionIter++;
		if (writingSectionIter != fwSections.end()) {
			writingAddr = writingSectionIter->lma;
		}
	} else {
		writingAddr += write_size;
	}
	writingSize = write_size;
}

void signetCmdResponse(void *cb_param, void *cmd_user_param, int cmd_token, int cmd, int end_device_state, int messages_remaining, int resp_code, void *resp_data)
{
	int token;
	switch(cmd) {
	case SIGNETDEV_CMD_STARTUP: {
		::signetdev_begin_update_firmware(NULL, &token);
	} break;
	case SIGNETDEV_CMD_BEGIN_UPDATE_FIRMWARE: {
		QByteArray erase_pages_;
		QByteArray page_mask(512, 0);

		if (resp_code != OKAY) {
			printf("Begin firmware update failed. Code %d\n", resp_code);
			QCoreApplication::quit();
			return;
		}

		printf("Starting to program...\n", resp_code);

		for (auto iter = fwSections.begin(); iter != fwSections.end(); iter++) {
			const fwSection &section = (*iter);
			unsigned int lma = section.lma;
			unsigned int lma_end = lma + section.size;
			int page_begin = (lma - 0x8000000)/2048;
			int page_end = (lma_end - 1 - 0x8000000)/2048;
			for (int i  = page_begin; i <= page_end; i++) {
				if (i < 0)
					continue;
				if (i >= 511)
					continue;
				page_mask[i] = 1;
			}
		}

		for (int i = 0; i < 512; i++) {
			if (page_mask[i]) {
				erase_pages_.push_back(i);
			}
		}
		::signetdev_erase_pages(NULL, &token, erase_pages_.size(), (u8 *)erase_pages_.data());
		} break;
	case SIGNETDEV_CMD_ERASE_PAGES:
		if (resp_code == OKAY) {
			::signetdev_get_progress(NULL, &token, 0, ERASING_PAGES);
		}
		break;
	case SIGNETDEV_CMD_WRITE_FLASH:
		if (resp_code == OKAY) {
			if (writingSectionIter == fwSections.end()) {
				::signetdev_reset_device(NULL, &token);
			} else {
				sendFirmwareWriteCmd();
			}
		}
		break;
	case SIGNETDEV_CMD_GET_PROGRESS: {
		signetdev_get_progress_resp_data *resp = (signetdev_get_progress_resp_data *)resp_data;
		if (end_device_state == FIRMWARE_UPDATE) {
			writingSectionIter = fwSections.begin();
			writingAddr = writingSectionIter->lma;
			sendFirmwareWriteCmd();
		} else {
			::signetdev_get_progress(NULL, &token, resp->total_progress, ERASING_PAGES);
		}
		} break;
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);
	int token;

	if (argc < 1)
		return 0;

	QFile firmware_update_file(argv[1]);
	bool result = firmware_update_file.open(QFile::ReadWrite);
	if (!result) {
		firmware_update_file.close();
		return 0;
	}

	QByteArray datum = firmware_update_file.readAll();
	QJsonDocument doc = QJsonDocument::fromJson(datum);

	firmware_update_file.close();

	bool valid_fw = !doc.isNull() && doc.isObject();

	QJsonObject doc_obj;
	QJsonObject sections_obj;

	if (valid_fw) {
		doc_obj = doc.object();
		QJsonValue temp_val = doc_obj.value("sections");
		valid_fw = (temp_val != QJsonValue::Undefined) && temp_val.isObject();
		if (valid_fw) {
			sections_obj = temp_val.toObject();
		}
	}

	if (valid_fw) {
		for (auto iter = sections_obj.constBegin(); iter != sections_obj.constEnd() && valid_fw; iter++) {
			fwSection section;
			section.name = iter.key();
			QJsonValue temp = iter.value();
			if (!temp.isObject()) {
				valid_fw = false;
				break;
			}

			QJsonObject section_obj = temp.toObject();
			QJsonValue lma_val = section_obj.value("lma");
			QJsonValue size_val = section_obj.value("size");
			QJsonValue contents_val = section_obj.value("contents");

			if (lma_val == QJsonValue::Undefined ||
			    size_val == QJsonValue::Undefined ||
			    contents_val == QJsonValue::Undefined ||
			    !lma_val.isDouble() ||
			    !size_val.isDouble() ||
			    !contents_val.isString()) {
				valid_fw = false;
				break;
			}
			section.lma = (unsigned int)(lma_val.toDouble());
			section.size = (unsigned int)(size_val.toDouble());
			section.contents = QByteArray::fromBase64(contents_val.toString().toLatin1());
			if (section.contents.size() != section.size) {
				valid_fw = false;
				break;
			}
			fwSections.append(section);
		}
	}

	if (!valid_fw)
		return 0;

	::signetdev_initialize_api();

	int rc = ::signetdev_open_connection();

	::signetdev_set_command_resp_cb(signetCmdResponse, NULL);
	::signetdev_set_device_closed_cb(deviceClosedS, NULL);
	::signetdev_set_error_handler(connectionErrorS, NULL);

	if (rc == OKAY) {
		::signetdev_startup(NULL, &token);
		return a.exec();
	}
	::signetdev_deinitialize_api();
}
