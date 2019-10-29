#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H


#define BUFFER_FIFO_MAX_STAGES 4
#define BUFFER_FIFO_MAX_BUFFERS 16

struct bufferFIFO {
	int maxBufferSize;
	int bufferCount;
	uint8_t *bufferStorage;
	int numStages;
	int ((*processStage[BUFFER_FIFO_MAX_STAGES])(struct bufferFIFO *bf, int readSize, const uint8_t *bufferRead, uint8_t *bufferWrite, int stageIdx));

	void (*processingComplete)(struct bufferFIFO *bf);
	int _bufferSize[BUFFER_FIFO_MAX_BUFFERS];
	int _stageWriteIndex[BUFFER_FIFO_MAX_STAGES];
	int _stageReadIndex[BUFFER_FIFO_MAX_STAGES];
	int _stalled[BUFFER_FIFO_MAX_STAGES];
	int _stageProcessing[BUFFER_FIFO_MAX_STAGES];
	int _processing;
	int _stall_index;
};

void bufferFIFO_processingComplete(struct bufferFIFO *bf, int stageIdx, int writeLen);
void bufferFIFO_start(struct bufferFIFO *bf, int firstBufferSize);
void bufferFIFO_stallStage(struct bufferFIFO *bf, int stageIdx);

#endif
