#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H


#define BUFFER_FIFO_MAX_STAGES 4
#define BUFFER_FIFO_MAX_BUFFERS 16

struct bufferFIFO {
	int maxBufferSize;
	int bufferCount;
	uint8_t *bufferStorage;
	int numStages;
	void ((*processStage[BUFFER_FIFO_MAX_STAGES])(struct bufferFIFO *bf, int readSize, u32 readData, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx));

	void (*processingComplete)(struct bufferFIFO *bf, u32 errorStatus, u32 errorStage);
	int _bufferSize[BUFFER_FIFO_MAX_BUFFERS];
	u32 _bufferData[BUFFER_FIFO_MAX_BUFFERS];

	u32 _stageErrorStatus[BUFFER_FIFO_MAX_STAGES];
	int _stageWriteIndex[BUFFER_FIFO_MAX_STAGES];
	int _stageReadIndex[BUFFER_FIFO_MAX_STAGES];

	int _stalled[BUFFER_FIFO_MAX_STAGES];
	int _stageProcessing[BUFFER_FIFO_MAX_STAGES];
	int _processing;
	int _stall_index;
	u32 _errorStatus;
	int _errorStage;
};

void bufferFIFO_processingComplete(struct bufferFIFO *bf, int stageIdx, int writeLen, u32 bufferData, u32 error);
void bufferFIFO_start(struct bufferFIFO *bf, int firstBufferSize);
void bufferFIFO_stallStage(struct bufferFIFO *bf, int stageIdx);

#endif
