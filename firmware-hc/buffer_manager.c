#include "main.h"
#include "buffer_manager.h"

static int bufferFIFO_stageStalled(struct bufferFIFO *bf, int stage);

static void bufferFIFO_execStage(struct bufferFIFO *bf, int stageIdx)
{
	int readBufferIdx = (bf->_stageReadIndex[stageIdx] % bf->bufferCount);
	int writeBufferIdx = (bf->_stageWriteIndex[stageIdx] % bf->bufferCount);
	bf->_stageProcessing[stageIdx] = 1;
	bf->processStage[stageIdx](bf,
	                           bf->_bufferSize[readBufferIdx],
	                           bf->_bufferData[readBufferIdx],
	                           bf->bufferStorage + bf->maxBufferSize * readBufferIdx,
	                           bf->bufferStorage + bf->maxBufferSize * writeBufferIdx,
	                           stageIdx);
}

static int bufferFIFO_stageStalled(struct bufferFIFO *bf, int stage)
{
	if (bf->_stageProcessing[stage] || bf->_stalled[stage]) {
		//Stage is stalled stalled if we aren't done writing to our destination buffer or
		//we stalled due to running out of data previously
		return 1;
	} else if (stage != 0) {
		int prevStage = stage - 1;
		if ((bf->_stageReadIndex[stage] == bf->_stageWriteIndex[prevStage])) {
			//We're stalled if the previous stage is writing to the buffer we are reading
			return 1;
		}
	} else {
		if (bf->_stageWriteIndex[0] == (bf->_stageReadIndex[bf->numStages - 1] + bf->bufferCount)) {
			return 1;
		}
	}
	return 0;
}

void bufferFIFO_stallStage(struct bufferFIFO *bf, int stageIdx)
{
	__disable_irq();
	bf->_stageProcessing[stageIdx] = 0;
	bf->_stalled[stageIdx] = 1;
	__enable_irq();
}

void bufferFIFO_processingComplete(struct bufferFIFO *bf, int stageIdx, int writeLen, u32 bufferData)
{
	__disable_irq();
	if (writeLen >= 0) {
		int i = bf->_stageWriteIndex[stageIdx] % bf->bufferCount;
		bf->_bufferSize[i] = writeLen;
		bf->_bufferData[i] = bufferData;
	}
	int nextStageIdx = (stageIdx + 1) % bf->numStages;
	//Advance read/write indexes
	bf->_stageProcessing[stageIdx] = 0;
	bf->_stageWriteIndex[stageIdx]++;
	bf->_stageReadIndex[stageIdx]++;
	//Process the next buffer if we aren't stalled by the next stage
	if (!bufferFIFO_stageStalled(bf, stageIdx))
		bufferFIFO_execStage(bf, stageIdx);
	if (!bufferFIFO_stageStalled(bf, nextStageIdx))
		bufferFIFO_execStage(bf, nextStageIdx);
	if (bf->_processing) {
		while (bf->_stall_index < bf->numStages) {
			if (bf->_stageProcessing[bf->_stall_index] || !bf->_stalled[bf->_stall_index])
				break;
			bf->_stall_index++;
		}
		if (bf->_stall_index == bf->numStages) {
			bf->_processing = 0;
			bf->processingComplete(bf);
		}
	}
	__enable_irq();
}

void bufferFIFO_start(struct bufferFIFO *bf, int firstBufferSize)
{
	__disable_irq();
	bf->_processing = 1;
	bf->_stall_index = 0;
	for (int i = 0; i < bf->bufferCount; i++) {
		bf->_bufferSize[i] = 0;
	}
	for (int i = 0; i < bf->numStages; i++) {
		bf->_stageProcessing[i] = 0;
		bf->_stalled[i] = 0;
		if (bf->numStages > 2) {
			if (i == 0) {
				bf->_stageReadIndex[i] = bf->numStages - 2;
				bf->_stageWriteIndex[i] = bf->numStages - 2;
			} else if (i == (bf->numStages - 1)) {
				bf->_stageReadIndex[i] = 0;
				bf->_stageWriteIndex[i] = 0;
			} else {
				bf->_stageReadIndex[i] = bf->numStages - 2 - i + 1;
				bf->_stageWriteIndex[i] = bf->numStages - 2 - i;
			}
		} else if (bf->numStages == 2) {
			bf->_stageReadIndex[i] = 0;
			bf->_stageWriteIndex[i] = 0;
		}
	}
	bf->_bufferSize[bf->_stageWriteIndex[0]] = firstBufferSize;
	bufferFIFO_execStage(bf, 0);
	__enable_irq();
}


