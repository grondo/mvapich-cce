#ifndef SIGHANDLER_H
#define SIGHANDLER_H

void initSignalHandler();
void disableSignal();

void revertSignal();

void updateSigAction(int pendingRecvs);

#endif
