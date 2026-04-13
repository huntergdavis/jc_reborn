#ifndef FOREGROUND_PILOT_H
#define FOREGROUND_PILOT_H

int foregroundPilotRequested(void);
void foregroundPilotSetScene(const char *sceneName);
void foregroundPilotPlay(void);
int foregroundPilotRuntimeStart(const char *sceneName);
void foregroundPilotRuntimeCompose(void);
void foregroundPilotRuntimeAdvance(void);
int foregroundPilotRuntimeActive(void);
void foregroundPilotRuntimeEnd(void);

#endif
