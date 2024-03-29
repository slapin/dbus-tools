#ifndef MODEMD_H
#define MODEMD_H
#define OFONO_SERVICE "org.ofono"
#define OFONO_MANAGER_PATH "/"
#define OFONO_MANAGER_INTERFACE OFONO_SERVICE ".Manager"
#define OFONO_MODEM_INTERFACE OFONO_SERVICE ".Modem"
#define OFONO_SIM_INTERFACE OFONO_SERVICE ".SimManager"
#define OFONO_NETREG_INTERFACE OFONO_SERVICE ".NetworkRegistration"
#define OFONO_CONNMAN_INTERFACE OFONO_SERVICE ".ConnectionManager"
#define OFONO_CONTEXT_INTERFACE OFONO_SERVICE ".ConnectionContext"
#define OFONO_VOICECALL_INTERFACE OFONO_SERVICE ".VoiceCallManager"
#define OFONO_CALL_INTERFACE OFONO_SERVICE ".VoiceCall"
#define OFONO_VOLUME_INTERFACE OFONO_SERVICE ".CallVolume"
void terminate_disable_modem(void);
#endif

