#if defined(CH_GEN2)
#include <signal.h>
#include <async_progress.h>
#include <stdio.h>
#include <assert.h>
#include "viapriv.h"
#include "mpid.h"

static int disableDepth = 0;

static struct sigaction oldact;
static struct sigaction currentAct;
#endif

void initSignalHandler() {
#if defined(CH_GEN2)
  sigaction(SIGUSR1, NULL, &currentAct);
  currentAct.sa_handler = SIG_IGN;
  sigaction(SIGUSR1, &currentAct, &oldact);
  oldact.sa_handler = SIG_IGN;
#endif
}

void sigusr1_handler() {
#if defined(CH_GEN2)
  MPID_DeviceCheck(MPID_NOTBLOCKING);
#endif
}

void disableSignal() {
#if defined(CH_GEN2)
  if(viadev_async_progress) {
    if (disableDepth == 0) {
      if (currentAct.sa_handler != SIG_IGN) {
        currentAct.sa_handler = SIG_IGN;
        sigaction(SIGUSR1, &currentAct, &oldact);
      }
      else {
        oldact.sa_handler = SIG_IGN;
      }
    }

    disableDepth++;
  }
#endif
}

void revertSignal() {
#if defined(CH_GEN2)
  if(viadev_async_progress) {
    disableDepth--;
    if(disableDepth == 0) {
      if(oldact.sa_handler != SIG_IGN) {
        currentAct.sa_handler = sigusr1_handler;
        sigaction(SIGUSR1, &currentAct, NULL);
      }
    }
  }
#endif
}

void updateSigAction(int pendingRecvs) {
#if defined(CH_GEN2)
  if(viadev_async_progress) {
    if(pendingRecvs > 0) {
      oldact.sa_handler = sigusr1_handler;
    } 
    else {
      oldact.sa_handler = SIG_IGN;
    }
  }
#endif
}
