#ifndef FOREGROUND_PILOT_H
#define FOREGROUND_PILOT_H

int foregroundPilotRequested(void);
const char *foregroundPilotSceneName(void);
void foregroundPilotSetScene(const char *sceneName);
void foregroundPilotPlay(void);
int foregroundPilotShouldStartForAds(const char *adsName, unsigned short adsTag);
int foregroundPilotRuntimeStartRequested(void);
int foregroundPilotRuntimeStartIfRequested(void);
int foregroundPilotRuntimeStart(const char *sceneName);
void foregroundPilotRuntimeCompose(void);
void foregroundPilotRuntimeAdvance(void);
int foregroundPilotRuntimeActive(void);
int foregroundPilotRuntimeMode(void);
unsigned short foregroundPilotRuntimeFrameIndex(void);
unsigned short foregroundPilotRuntimeSourceFrame(void);
unsigned short foregroundPilotRuntimeDisplayVBlanks(void);
int foregroundPilotRuntimeHasFrameData(void);
int foregroundPilotRuntimeRequestedEver(void);
int foregroundPilotRuntimeStartedEver(void);
int foregroundPilotRuntimeComposedEver(void);
void foregroundPilotRuntimeEnd(void);

#endif
