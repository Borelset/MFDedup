//
// Created by Borelset on 2019/7/29.
//
//  Copyright (C) 2020-present, Xiangyu Zou. All rights reserved.
//  This source code is licensed under the GPLv2

#ifndef MFDEDUP_WRITEFILEPIPELINE_H
#define MFDEDUP_WRITEFILEPIPELINE_H


#include "jemalloc/jemalloc.h"
#include "../MetadataManager/MetadataManager.h"
#include "../Utility/ChunkWriterManager.h"
#include "GCPipieline.h"

DEFINE_string(LogicFilePath,
"/data/MFDedupHome/logicFiles/%lu", "recipe path");

struct BlockHeader {
    SHA1FP fp;
    uint64_t length;
};


class WriteFilePipeline {
public:
    WriteFilePipeline() : runningFlag(true), taskAmount(0), mutexLock(), condition(mutexLock),
                          logicFileOperator(nullptr) {
        worker = new std::thread(std::bind(&WriteFilePipeline::writeFileCallback, this));
    }

    int addTask(const WriteTask &writeTask) {
        MutexLockGuard mutexLockGuard(mutexLock);
        receiveList.push_back(writeTask);
        taskAmount++;
        condition.notify();
    }

    ~WriteFilePipeline() {
        runningFlag = false;
        condition.notifyAll();
        worker->join();
    }

    void getStatistics() {
        printf("write duration:%lu\n", duration);
    }

private:
    void writeFileCallback() {
        struct timeval t0, t1;
        bool newVersionFlag = true;

        BlockHeader blockHeader;
        ChunkWriterManager *chunkWriterManager = nullptr;

        while (runningFlag) {
            {
                MutexLockGuard mutexLockGuard(mutexLock);
                while (!taskAmount) {
                    condition.wait();
                    if (!runningFlag) break;
                }
                if (!runningFlag) continue;
                taskAmount = 0;
                condition.notify();
                taskList.swap(receiveList);
            }

            gettimeofday(&t0, NULL);

            if (chunkWriterManager == nullptr) {
                currentVersion++;
                chunkWriterManager = new ChunkWriterManager(currentVersion);
            }

            for (auto &writeTask : taskList) {

                if (!logicFileOperator) {
                    sprintf(buffer, FLAGS_LogicFilePath.c_str(), writeTask.fileID);
                    logicFileOperator = new FileOperator(buffer, FileOpenType::Write);
                }
                blockHeader = {
                        writeTask.sha1Fp,
                        writeTask.bufferLength,
                };
                switch (writeTask.type) {
                    case 0:
                        chunkWriterManager->writeClass((currentVersion + 1) * currentVersion / 2,
                                                       (uint8_t * ) & blockHeader, sizeof(BlockHeader),
                                                       writeTask.buffer + writeTask.pos, writeTask.bufferLength);
                        logicFileOperator->write((uint8_t * ) & blockHeader, sizeof(BlockHeader));
                        break;
                    case 1:
                        logicFileOperator->write((uint8_t * ) & blockHeader, sizeof(BlockHeader));
                        break;
                    case 2:
                        chunkWriterManager->writeClass(writeTask.oldClass + currentVersion - 1,
                                                       (uint8_t * ) & blockHeader, sizeof(BlockHeader),
                                                       writeTask.buffer + writeTask.pos, writeTask.bufferLength);
                        logicFileOperator->write((uint8_t * ) & blockHeader, sizeof(BlockHeader));
                        break;

                }

                if (writeTask.countdownLatch) {
                    printf("WritePipeline finish\n");
                    logicFileOperator->fsync();
                    delete logicFileOperator;
                    logicFileOperator = nullptr;
                    writeTask.countdownLatch->countDown();
                    free(writeTask.buffer);

                    delete chunkWriterManager;
                    chunkWriterManager = nullptr;
                }

            }
            taskList.clear();

            gettimeofday(&t1, NULL);
            duration += (t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec;
        }
    }

    FileOperator *logicFileOperator;
    char buffer[256];
    bool runningFlag;
    std::thread *worker;
    uint64_t taskAmount;
    std::list <WriteTask> taskList;
    std::list <WriteTask> receiveList;
    MutexLock mutexLock;
    Condition condition;
    uint64_t duration = 0;

    uint64_t currentVersion = 0;
};

static WriteFilePipeline *GlobalWriteFilePipelinePtr;

#endif //MFDEDUP_WRITEFILEPIPELINE_H
