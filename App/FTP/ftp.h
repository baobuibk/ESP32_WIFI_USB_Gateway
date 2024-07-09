#ifndef FTP_H_
#define FTP_H_

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>
#include <stdbool.h>
#include <sys/dirent.h>
#include <sys/unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*********************
 *      DEFINES
 *********************/

#define FTP_CMD_PORT                        21
#define FTP_ACTIVE_DATA_PORT                20
#define FTP_PASIVE_DATA_PORT                2024
#define FTP_CMD_SIZE_MAX                    6
#define FTP_CMD_CLIENTS_MAX                 1
#define FTP_DATA_CLIENTS_MAX                1
#define FTP_MAX_PARAM_SIZE                  (MICROPY_ALLOC_PATH_MAX + 1)
#define FTP_UNIX_SECONDS_180_DAYS           15552000
#define FTP_DATA_TIMEOUT_MS                 10000   // 10 seconds
#define FTP_SOCKETFIFO_ELEMENTS_MAX         4

#define CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE 1024 * 100
#define CONFIG_MICROPY_FTPSERVER_TIMEOUT 300
#define CONFIG_MICROPY_FILESYSTEM_TYPE 0
#define MICROPY_ALLOC_PATH_MAX (512)

#define FTP_USER_PASS_LEN_MAX	32
#define FTP_DEF_USER            "micro"
#define FTP_DEF_PASS            "python"
#define FTP_MUTEX_TIMEOUT_MS    1000
#define FTP_CMD_TIMEOUT_MS      (CONFIG_MICROPY_FTPSERVER_TIMEOUT*1000)


#define VFS_NATIVE_MOUNT_POINT          "/_#!#_spiffs"
#define VFS_NATIVE_SDCARD_MOUNT_POINT   "/_#!#_sdcard"
#define VFS_NATIVE_INTERNAL_PART_LABEL  "internalfs"
#define VFS_NATIVE_INTERNAL_MP          "/flash"
#define VFS_NATIVE_EXTERNAL_MP          "/sd"
#define VFS_NATIVE_TYPE_SPIFLASH        0
#define VFS_NATIVE_TYPE_SDCARD          1

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

/**********************
 *      TYPEDEFS
 **********************/

typedef enum 
{
    E_FTP_STE_DISABLED = 0,
    E_FTP_STE_START,
    E_FTP_STE_READY,
    E_FTP_STE_END_TRANSFER,
    E_FTP_STE_CONTINUE_LISTING,
    E_FTP_STE_CONTINUE_FILE_TX,
    E_FTP_STE_CONTINUE_FILE_RX,
    E_FTP_STE_CONNECTED
} ftp_state_t;

typedef enum 
{
    E_FTP_STE_SUB_DISCONNECTED = 0,
    E_FTP_STE_SUB_LISTEN_FOR_DATA,
    E_FTP_STE_SUB_DATA_CONNECTED
} ftp_substate_t;

typedef enum 
{
    E_FTP_RESULT_OK = 0,
    E_FTP_RESULT_CONTINUE,
    E_FTP_RESULT_FAILED
} ftp_result_t;

typedef struct 
{
    bool            uservalid : 1;
    bool            passvalid : 1;
} ftp_loggin_t;

typedef enum 
{
    E_FTP_NOTHING_OPEN = 0,
    E_FTP_FILE_OPEN,
    E_FTP_DIR_OPEN
} ftp_e_open_t;

typedef enum 
{
    E_FTP_CLOSE_NONE = 0,
    E_FTP_CLOSE_DATA,
    E_FTP_CLOSE_CMD_AND_DATA,
} ftp_e_closesocket_t;

typedef struct 
{
    uint8_t         *dBuffer;
    uint32_t        ctimeout;
    union 
    {
        DIR         *dp;
        FILE        *fp;
    };
    int32_t         lc_sd;
    int32_t         ld_sd;
    int32_t         c_sd;
    int32_t         d_sd;
    int32_t         dtimeout;
    uint32_t        ip_addr;
    uint32_t        total;
    uint32_t        time;
    uint8_t         state;
    uint8_t         substate;
    uint8_t         txRetries;
    uint8_t         logginRetries;
    ftp_loggin_t    loggin;
    uint8_t         e_open;
    bool            closechild;
    bool            enabled;
    bool            listroot;
} ftp_data_t;

typedef struct 
{
    char * cmd;
} ftp_cmd_t;

typedef enum 
{
    E_FTP_CMD_NOT_SUPPORTED = -1,
    E_FTP_CMD_FEAT = 0,
    E_FTP_CMD_SYST,
    E_FTP_CMD_CDUP,
    E_FTP_CMD_CWD,
    E_FTP_CMD_PWD,
    E_FTP_CMD_XPWD,
    E_FTP_CMD_SIZE,
    E_FTP_CMD_MDTM,
    E_FTP_CMD_TYPE,
    E_FTP_CMD_USER,
    E_FTP_CMD_PASS,
    E_FTP_CMD_PASV,
    E_FTP_CMD_LIST,
    E_FTP_CMD_RETR,
    E_FTP_CMD_STOR,
    E_FTP_CMD_DELE,
    E_FTP_CMD_RMD,
    E_FTP_CMD_MKD,
    E_FTP_CMD_RNFR,
    E_FTP_CMD_RNTO,
    E_FTP_CMD_NOOP,
    E_FTP_CMD_QUIT,
    E_FTP_CMD_APPE,
    E_FTP_CMD_NLST,
    E_FTP_CMD_AUTH,
    E_FTP_NUM_FTP_CMDS
} ftp_cmd_index_t;


/**********************
 *   PUBLIC FUNCTIONS
 **********************/

bool ftp_init (void);
void ftp_deinit (void);
int ftp_run (uint32_t elapsed);
bool ftp_enable (void);
bool ftp_isenabled (void);
bool ftp_disable (void);
bool ftp_reset (void);
int ftp_getstate();
bool ftp_terminate (void);
bool ftp_stop_requested();

#ifdef __cplusplus
}
#endif

#endif /* FTP_H_ */