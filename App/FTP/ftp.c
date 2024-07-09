/*********************
 *      INCLUDES
 *********************/

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "ftp.h"
#include "sd_card.h"

/***********************************
 *      DEFINES
 ***********************************/

#define FTP_TAG  "[Ftp]"

/***********************************
 *           DATA
 ***********************************/

char ftp_user[FTP_USER_PASS_LEN_MAX + 1];
char ftp_pass[FTP_USER_PASS_LEN_MAX + 1];

/***********************************
 *   PRIVATE DATA
 ***********************************/

static ftp_data_t ftp_data = {0};
static char *ftp_path = NULL;
static char *ftp_scratch_buffer = NULL;
static char *ftp_cmd_buffer = NULL;
static uint8_t ftp_nlist = 0;
static const ftp_cmd_t ftp_cmd_table[] = 
{   { "FEAT" }, { "SYST" }, { "CDUP" }, { "CWD"	},
    { "PWD"	}, { "XPWD" }, { "SIZE" }, { "MDTM" },
    { "TYPE" }, { "USER" }, { "PASS" }, { "PASV" },
    { "LIST" }, { "RETR" }, { "STOR" }, { "DELE" },
    { "RMD"	}, { "MKD"	}, { "RNFR" }, { "RNTO" },
    { "NOOP" }, { "QUIgT" }, { "APPE" }, { "NLST" }, 
    { "AUTH" } 
};

int ftp_buff_size = CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE;
int ftp_timeout = FTP_CMD_TIMEOUT_MS;

static uint8_t ftp_stop = 0;

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

// ******** File Function ******************************

static bool ftp_open_file(const char *path, const char *mode);
static void ftp_close_files_dir(void);
static void ftp_close_filesystem_on_error(void);
static ftp_result_t ftp_read_file(char *filebuf, uint32_t desiredsize,
                                  uint32_t *actualsize);
static ftp_result_t ftp_write_file(char *filebuf, uint32_t size);
static ftp_result_t ftp_open_dir_for_listing(const char *path);
static int ftp_get_eplf_item(char *dest, uint32_t destsize, struct dirent *de);
static ftp_result_t ftp_list_dir(char *list, uint32_t maxlistsize,
                                 uint32_t *listsize);

// ******** Socket Function *****************************
static void ftp_close_cmd_data(void);
static void _ftp_reset(void);
static bool ftp_create_listening_socket(int32_t *sd, uint32_t port, uint8_t backlog);
static ftp_result_t ftp_wait_for_connection(int32_t l_sd, int32_t *n_sd, uint32_t *ip_addr);
static void ftp_send_reply(uint32_t status, char *message);
static void ftp_send_list(uint32_t datasize);
static void ftp_send_file_data(uint32_t datasize);
static ftp_result_t ftp_recv_non_blocking(int32_t sd, void *buff, int32_t Maxlen, int32_t *rxLen);

// ******** Directory Function **************************

static void ftp_open_child(char *pwd, char *dir);
static void ftp_close_child(char *pwd);
static void remove_fname_from_path(char *pwd, char *fname);

// ******** Param functions **************************

static void ftp_pop_param(char **str, char *param, bool stop_on_space, bool stop_on_newline);
static ftp_cmd_index_t ftp_pop_command(char **str);
static void ftp_get_param_and_open_child(char **bufptr);

// ******** Ftp command processing **************************

static void ftp_process_cmd(void);
static void ftp_wait_for_enabled(void);

// **********************************

/**
 * The function `stoupper` converts all characters in a string to uppercase.
 * 
 * @param str The parameter `str` in the `stoupper` function is a pointer to a character array (string)
 * that will be converted to uppercase characters.
 */
static void stoupper (char *str) 
{
	while (str && *str != '\0') 
    {
		*str = (char)toupper((int)(*str));
		str++;
	}
}

/***********************************
 *   PUBLIC FUNCTIONS
 ***********************************/

/**
 * The function `ftp_init` initializes FTP-related data structures and memory allocations, returning
 * true if successful.
 *
 * @return The function `ftp_init` returns a boolean value, either `true` if the initialization process
 * is successful, or `false` if there is an error during initialization.
 */
bool ftp_init(void)
{
    ftp_stop = 0;
    // Allocate memory for the data buffer, and the file system structures (from the RTOS heap)
    ftp_deinit();

    memset(&ftp_data, 0, sizeof(ftp_data_t));
    ftp_data.dBuffer = malloc(ftp_buff_size + 1);

    if (ftp_data.dBuffer == NULL)
        return false;

    ftp_path = malloc(FTP_MAX_PARAM_SIZE);

    if (ftp_path == NULL)
    {
        free(ftp_data.dBuffer);
        return false;
    }

    ftp_scratch_buffer = malloc(FTP_MAX_PARAM_SIZE);

    if (ftp_scratch_buffer == NULL)
    {
        free(ftp_path);
        free(ftp_data.dBuffer);
        return false;
    }

    ftp_cmd_buffer = malloc(FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX);

    if (ftp_cmd_buffer == NULL)
    {
        free(ftp_scratch_buffer);
        free(ftp_path);
        free(ftp_data.dBuffer);
        return false;
    }

    ftp_data.c_sd = -1;
    ftp_data.d_sd = -1;
    ftp_data.lc_sd = -1;
    ftp_data.ld_sd = -1;
    ftp_data.e_open = E_FTP_NOTHING_OPEN;
    ftp_data.state = E_FTP_STE_DISABLED;
    ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;

    return true;
}

/**
 * The function ftp_deinit deallocates memory for various FTP-related buffers and sets their pointers
 * to NULL.
 */
void ftp_deinit(void)
{
    if (ftp_path)
        free(ftp_path);
    if (ftp_cmd_buffer)
        free(ftp_cmd_buffer);
    if (ftp_data.dBuffer)
        free(ftp_data.dBuffer);
    if (ftp_scratch_buffer)
        free(ftp_scratch_buffer);

    ftp_path = NULL;
    ftp_cmd_buffer = NULL;
    ftp_data.dBuffer = NULL;
    ftp_scratch_buffer = NULL;
}

/**
 * The function `ftp_run` manages the FTP server state and data transfer operations.
 *
 * @param elapsed The `elapsed` parameter in the `ftp_run` function represents the time elapsed since
 * the last invocation of the function. It is used to update various timeout and time-related variables
 * within the FTP server implementation.
 *
 * @return The function `ftp_run` is returning an integer value of 0.
 */
int ftp_run (uint32_t elapsed)
{
	//if (xSemaphoreTake(ftp_mutex, FTP_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;
	if (ftp_stop) return -2;

	ftp_data.dtimeout += elapsed;
	ftp_data.ctimeout += elapsed;
	ftp_data.time += elapsed;

	switch (ftp_data.state) {
		case E_FTP_STE_DISABLED:
			ftp_wait_for_enabled();
			break;
		case E_FTP_STE_START:
			if (ftp_create_listening_socket(&ftp_data.lc_sd, FTP_CMD_PORT, FTP_CMD_CLIENTS_MAX - 1)) {
				ftp_data.state = E_FTP_STE_READY;
			}
			break;
		case E_FTP_STE_READY:
			if (ftp_data.c_sd < 0 && ftp_data.substate == E_FTP_STE_SUB_DISCONNECTED) {
				if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.lc_sd, &ftp_data.c_sd, &ftp_data.ip_addr)) {
					ftp_data.txRetries = 0;
					ftp_data.logginRetries = 0;
					ftp_data.ctimeout = 0;
					ftp_data.loggin.uservalid = false;
					ftp_data.loggin.passvalid = false;
					strcpy (ftp_path, "/");
					ESP_LOGI(FTP_TAG, "Connected.");
					//ftp_send_reply (220, "Micropython FTP Server");
					ftp_send_reply (220, "ESP32 FTP Server");
					break;
				}
			}
			if (ftp_data.c_sd > 0 && ftp_data.substate != E_FTP_STE_SUB_LISTEN_FOR_DATA) {
				ftp_process_cmd();
				if (ftp_data.state != E_FTP_STE_READY) {
					break;
				}
			}
			break;
		case E_FTP_STE_END_TRANSFER:
			if (ftp_data.d_sd >= 0) {
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
			}
			break;
		case E_FTP_STE_CONTINUE_LISTING:
			// go on with listing
			{
				uint32_t listsize = 0;
				ftp_result_t list_res = ftp_list_dir((char *)ftp_data.dBuffer, ftp_buff_size, &listsize);
				if (listsize > 0) ftp_send_list(listsize);
				if (list_res == E_FTP_RESULT_OK) {
					ftp_send_reply(226, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
				}
				ftp_data.ctimeout = 0;
			}
			break;
		case E_FTP_STE_CONTINUE_FILE_TX:
			// read and send the next block from the file
			{
				uint32_t readsize;
				ftp_result_t result;
				ftp_data.ctimeout = 0;
				result = ftp_read_file ((char *)ftp_data.dBuffer, ftp_buff_size, &readsize);
				if (result == E_FTP_RESULT_FAILED) {
					ftp_send_reply(451, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
				}
				else {
					if (readsize > 0) {
						ftp_send_file_data(readsize);
						ftp_data.total += readsize;
						ESP_LOGI(FTP_TAG, "Sent %"PRIu32", total: %"PRIu32, readsize, ftp_data.total);
					}
					if (result == E_FTP_RESULT_OK) {
						ftp_send_reply(226, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGI(FTP_TAG, "File sent (%"PRIu32" bytes in %"PRIu32" msec).", ftp_data.total, ftp_data.time);
					}
				}
			}
			break;
		case E_FTP_STE_CONTINUE_FILE_RX:
			{
				int32_t len;
				ftp_result_t result = E_FTP_RESULT_OK;

				ESP_LOGI(FTP_TAG, "ftp_buff_size=%d", ftp_buff_size);
				result = ftp_recv_non_blocking(ftp_data.d_sd, ftp_data.dBuffer, ftp_buff_size, &len);
				if (result == E_FTP_RESULT_OK) {
					// block of data received
					ftp_data.dtimeout = 0;
					ftp_data.ctimeout = 0;
					// save received data to file
					if (E_FTP_RESULT_OK != ftp_write_file ((char *)ftp_data.dBuffer, len)) {
						ftp_send_reply(451, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGW(FTP_TAG, "Error writing to file");
					}
					else {
						ftp_data.total += len;
						ESP_LOGI(FTP_TAG, "Received %"PRIu32", total: %"PRIu32, len, ftp_data.total);
					}
				}
				else if (result == E_FTP_RESULT_CONTINUE) {
					// nothing received
					if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
						ftp_close_files_dir();
						ftp_send_reply(426, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGW(FTP_TAG, "Receiving to file timeout");
					}
				}
				else {
					// File received (E_FTP_RESULT_FAILED)
					ftp_close_files_dir();
					ftp_send_reply(226, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ESP_LOGI(FTP_TAG, "File received (%"PRIu32" bytes in %"PRIu32" msec).", ftp_data.total, ftp_data.time);
					break;
				}
			}
			break;
		default:
			break;
	}

	switch (ftp_data.substate) {
	case E_FTP_STE_SUB_DISCONNECTED:
		break;
	case E_FTP_STE_SUB_LISTEN_FOR_DATA:
		if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.ld_sd, &ftp_data.d_sd, NULL)) {
			ftp_data.dtimeout = 0;
			ftp_data.substate = E_FTP_STE_SUB_DATA_CONNECTED;
			ESP_LOGI(FTP_TAG, "Data socket connected");
		}
		else if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
			ESP_LOGW(FTP_TAG, "Waiting for data connection timeout (%"PRIi32")", ftp_data.dtimeout);
			ftp_data.dtimeout = 0;
			// close the listening socket
			closesocket(ftp_data.ld_sd);
			ftp_data.ld_sd = -1;
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
		}
		break;
	case E_FTP_STE_SUB_DATA_CONNECTED:
		if (ftp_data.state == E_FTP_STE_READY && (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS)) {
			// close the listening and the data socket
			closesocket(ftp_data.ld_sd);
			closesocket(ftp_data.d_sd);
			ftp_data.ld_sd = -1;
			ftp_data.d_sd = -1;
			ftp_close_filesystem_on_error ();
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
			ESP_LOGW(FTP_TAG, "Data connection timeout");
		}
		break;
	default:
		break;
	}

	// check the state of the data sockets
	if (ftp_data.d_sd < 0 && (ftp_data.state > E_FTP_STE_READY)) {
		ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
		ftp_data.state = E_FTP_STE_READY;
		ESP_LOGI(FTP_TAG, "Data socket disconnected");
	}

	//xSemaphoreGive(ftp_mutex);
	return 0;
}

/**
 * The function `ftp_enable` sets the FTP enabled state to true if it was previously disabled.
 *
 * @return The function `ftp_enable` is returning a boolean value. If the condition `ftp_data.state ==
 * E_FTP_STE_DISABLED` is true, then the function sets `ftp_data.enabled` to true and returns `true`.
 * Otherwise, it returns `false`.
 */
bool ftp_enable(void)
{
    bool res = false;

    if (ftp_data.state == E_FTP_STE_DISABLED)
    {
        ftp_data.enabled = true;
        res = true;
    }

    return res;
}

/**
 * The function ftp_isenabled checks if FTP is enabled and returns a boolean value accordingly.
 *
 * @return The function `ftp_isenabled` is returning a boolean value that indicates whether FTP is
 * enabled or not. It checks the value of `ftp_data.enabled` and returns `true` if it is enabled, and
 * `false` if it is not enabled.
 */
bool ftp_isenabled(void)
{
    bool res = (ftp_data.enabled == true);
    return res;
}

/**
 * The function `ftp_disable` disables FTP if it is in the ready state.
 *
 * @return The function `ftp_disable` returns a boolean value, either `true` or `false`.
 */
bool ftp_disable(void)
{
    bool res = false;

    if (ftp_data.state == E_FTP_STE_READY)
    {
        _ftp_reset();
        ftp_data.enabled = false;
        ftp_data.state = E_FTP_STE_DISABLED;
        res = true;
    }

    return res;
}

/**
 * The function ftp_reset calls _ftp_reset and returns true.
 *
 * @return The function `ftp_reset` is returning a boolean value `true`.
 */
bool ftp_reset(void)
{
    _ftp_reset();
    return true;
}

/**
 * The function ftp_getstate returns the current state of the FTP connection based on the values of
 * ftp_data.state and ftp_data.substate.
 *
 * @return The function `ftp_getstate` returns the current state of the FTP connection. If the FTP
 * state is `E_FTP_STE_READY` and the control socket `c_sd` is greater than 0, then it returns
 * `E_FTP_STE_CONNECTED`. Otherwise, it returns the combined state and substate values.
 */
int ftp_getstate()
{
    int fstate = ftp_data.state | (ftp_data.substate << 8);

    if ((ftp_data.state == E_FTP_STE_READY) && (ftp_data.c_sd > 0))
        fstate = E_FTP_STE_CONNECTED;

    return fstate;
}

/**
 * The function `ftp_terminate` checks if the FTP data state is ready, stops the FTP process, resets
 * it, and returns a boolean indicating success.
 *
 * @return The function `ftp_terminate` returns a boolean value, either `true` or `false`, based on the
 * conditions inside the function.
 */
bool ftp_terminate(void)
{
    bool res = false;

    if (ftp_data.state == E_FTP_STE_READY)
    {
        ftp_stop = 1;
        _ftp_reset();
        res = true;
    }

    return res;
}

/**
 * The function ftp_stop_requested() returns true if the ftp_stop variable is equal to 1.
 *
 * @return a boolean value indicating whether the `ftp_stop` variable is equal to 1.
 */
bool ftp_stop_requested()
{
    bool res = (ftp_stop == 1);
    return res;
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/

// ******** File Function ******************************
/**
 * The function `ftp_open_file` opens a file specified by the given path and mode, logging information
 * and returning a boolean indicating success or failure.
 *
 * @param path The `path` parameter in the `ftp_open_file` function is a pointer to a string that
 * represents the file path or filename that you want to open or operate on. It is used to construct
 * the full path to the file by concatenating it with the `MOUNT_POINT` variable.
 * @param mode The `mode` parameter in the `ftp_open_file` function specifies the mode in which the
 * file should be opened. It is a string that indicates how the file should be accessed. Some common
 * modes include:
 *
 * @return The function `ftp_open_file` returns a boolean value, either `true` or `false`.
 */

static bool ftp_open_file(const char *path, const char *mode)
{
    ESP_LOGI(FTP_TAG, "ftp_open_file: path=[%s]", path);
    char fullname[128];
    strcpy(fullname, MOUNT_POINT);
    strcat(fullname, path);
    ESP_LOGI(FTP_TAG, "ftp_open_file: fullname=[%s]", fullname);
    ftp_data.fp = fopen(fullname, mode);
    if (ftp_data.fp == NULL)
    {
        ESP_LOGE(FTP_TAG, "ftp_open_file: open fail [%s]", fullname);
        return false;
    }
    ftp_data.e_open = E_FTP_FILE_OPEN;
    return true;
}

/**
 * The function `ftp_close_files_dir` closes either a file or a directory based on the current state of
 * the FTP data.
 */
static void ftp_close_files_dir(void)
{
    if (ftp_data.e_open == E_FTP_FILE_OPEN)
    {
        fclose(ftp_data.fp);
        ftp_data.fp = NULL;
    }
    else if (ftp_data.e_open == E_FTP_DIR_OPEN)
    {
        closedir(ftp_data.dp);
        ftp_data.dp = NULL;
    }
    ftp_data.e_open = E_FTP_NOTHING_OPEN;
}

/**
 * The function ftp_close_filesystem_on_error closes files and directories in an FTP filesystem on
 * error.
 */
static void ftp_close_filesystem_on_error(void)
{
    ftp_close_files_dir();
    if (ftp_data.fp)
    {
        fclose(ftp_data.fp);
        ftp_data.fp = NULL;
    }
    if (ftp_data.dp)
    {
        closedir(ftp_data.dp);
        ftp_data.dp = NULL;
    }
}

/**
 * The function `ftp_read_file` reads a file into a buffer and returns the result based on the desired
 * and actual sizes.
 *
 * @param filebuf The `filebuf` parameter is a pointer to a buffer where the data read from the FTP
 * file will be stored.
 * @param desiredsize The `desiredsize` parameter in the `ftp_read_file` function represents the size
 * of data that the caller wants to read from the file. It indicates the number of bytes that the
 * caller intends to read from the file into the `filebuf` buffer.
 * @param actualsize The `actualsize` parameter in the `ftp_read_file` function is a pointer to a
 * `uint32_t` variable. This parameter is used to store the actual size of the data read from the file
 * into the `filebuf` buffer. The function updates the value pointed to by `actual
 *
 * @return The function `ftp_read_file` returns a value of type `ftp_result_t`, which is either
 * `E_FTP_RESULT_CONTINUE`, `E_FTP_RESULT_FAILED`, or `E_FTP_RESULT_OK` based on the conditions within
 * the function.
 */
static ftp_result_t ftp_read_file(char *filebuf, uint32_t desiredsize, uint32_t *actualsize)
{
    ftp_result_t result = E_FTP_RESULT_CONTINUE;
    *actualsize = fread(filebuf, 1, desiredsize, ftp_data.fp);
    if (*actualsize == 0)
    {
        ftp_close_files_dir();
        result = E_FTP_RESULT_FAILED;
    }
    else if (*actualsize < desiredsize)
    {
        ftp_close_files_dir();
        result = E_FTP_RESULT_OK;
    }
    return result;
}

/**
 * The function `ftp_write_file` writes a file buffer to an FTP server and returns a result indicating
 * success or failure.
 *
 * @param filebuf The `filebuf` parameter in the `ftp_write_file` function is a pointer to a buffer
 * containing the data that needs to be written to a file.
 * @param size The `size` parameter in the `ftp_write_file` function represents the size of the data to
 * be written to the file. It is of type `uint32_t`, which is an unsigned 32-bit integer. This
 * parameter specifies the number of bytes to be written from the `filebuf`
 *
 * @return The function `ftp_write_file` is returning a variable of type `ftp_result_t`, which is
 * either `E_FTP_RESULT_OK` if the write operation was successful, or `E_FTP_RESULT_FAILED` if it was
 * not successful.
 */
static ftp_result_t ftp_write_file(char *filebuf, uint32_t size)
{
    ftp_result_t result = E_FTP_RESULT_FAILED;
    uint32_t actualsize = fwrite(filebuf, 1, size, ftp_data.fp);

    if (actualsize == size)
    {
        result = E_FTP_RESULT_OK;
    }
    else
    {
        ftp_close_files_dir();
    }
    return result;
}

/**
 * The function `ftp_open_dir_for_listing` opens a directory for listing files and returns a result
 * based on the success of the operation.
 *
 * @param path The `path` parameter in the `ftp_open_dir_for_listing` function represents the directory
 * path that you want to open for listing. It is a string containing the directory path relative to the
 * `MOUNT_POINT` directory.
 *
 * @return E_FTP_RESULT_CONTINUE
 */
static ftp_result_t ftp_open_dir_for_listing(const char *path)
{
    char fullname[128];
    strcpy(fullname, MOUNT_POINT);
    strcat(fullname, path);

    if (ftp_data.dp)
    {
        closedir(ftp_data.dp);
        ftp_data.dp = NULL;
    }

    ESP_LOGI(FTP_TAG, "ftp_open_dir_for_listing path=[%s] MOUNT_POINT=[%s]",
             path, MOUNT_POINT);
    ESP_LOGI(FTP_TAG, "ftp_open_dir_for_listing: %s", fullname);

    ftp_data.dp = opendir(fullname); // Open the directory

    if (ftp_data.dp == NULL)
    {
        return E_FTP_RESULT_FAILED;
    }

    ftp_data.e_open = E_FTP_DIR_OPEN;
    ftp_data.listroot = false;

    return E_FTP_RESULT_CONTINUE;
}

/**
 * The function `ftp_get_eplf_item` generates a formatted listing of a file or directory with details
 * like permissions, size, and modification time.
 * 
 * @param dest The `dest` parameter in the `ftp_get_eplf_item` function is a pointer to a character
 * array where the function will write the formatted output. It represents the destination buffer where
 * the function will store the information about the file or directory specified by the `struct dirent
 * *de` parameter.
 * @param destsize The `destsize` parameter in the `ftp_get_eplf_item` function represents the size of
 * the destination buffer `dest` that is passed to the function. It indicates the maximum number of
 * characters that can be stored in the `dest` buffer to avoid buffer overflow issues. The function
 * uses
 * @param de The `de` parameter in the `ftp_get_eplf_item` function is a pointer to a `struct dirent`
 * object. This object typically represents a directory entry and contains information about a file or
 * directory, such as its name and type. The function uses this information to construct a detailed
 * listing
 * 
 * @return The function `ftp_get_eplf_item` is returning the size of the data written to the `dest`
 * buffer after formatting the directory entry information.
 */
static int ftp_get_eplf_item (char *dest, uint32_t destsize, struct dirent *de) 
{

	char *type = (de->d_type & DT_DIR) ? "d" : "-";

	// Get full file path needed for stat function
	char fullname[128];
	strcpy(fullname, MOUNT_POINT);
	strcat(fullname, ftp_path);
	//strcpy(fullname, ftp_path);
	if (fullname[strlen(fullname)-1] != '/') strcat(fullname, "/");
	strcat(fullname, de->d_name);

	struct stat buf;
	int res = stat(fullname, &buf);
	ESP_LOGI(FTP_TAG, "ftp_get_eplf_item res=%d buf.st_size=%ld", res, buf.st_size);
	
    if (res < 0) 
    {
		buf.st_size = 0;
		buf.st_mtime = 946684800; // Jan 1, 2000
	}

	char str_time[64];
	struct tm *tm_info;
	time_t now;
	if (time(&now) < 0) now = 946684800;	// get the current time from the RTC
	tm_info = localtime(&buf.st_mtime);		// get broken-down file time

	// if file is older than 180 days show dat,month,year else show month, day and time
	if ((buf.st_mtime + FTP_UNIX_SECONDS_180_DAYS) < now) strftime(str_time, 127, "%b %d %Y", tm_info);
	else strftime(str_time, 63, "%b %d %H:%M", tm_info);

	int addsize = destsize + 64;

	while (addsize >= destsize) 
    {
		if (ftp_nlist) addsize = snprintf(dest, destsize, "%s\r\n", de->d_name);
		else addsize = snprintf(dest, destsize, "%srw-rw-rw-   1 root  root %9"PRIu32" %s %s\r\n", type, (uint32_t)buf.st_size, str_time, de->d_name);
		if (addsize >= destsize) 
        {
			ESP_LOGW(FTP_TAG, "Buffer too small, reallocating [%d > %"PRIi32"]", ftp_buff_size, ftp_buff_size + (addsize - destsize) + 64);
			char *new_dest = realloc(dest, ftp_buff_size + (addsize - destsize) + 65);
			if (new_dest) 
            {
				ftp_buff_size += (addsize - destsize) + 64;
				destsize += (addsize - destsize) + 64;
				dest = new_dest;
				addsize = destsize + 64;
			}
			else 
            {
				ESP_LOGE(FTP_TAG, "Buffer reallocation ERROR");
				addsize = 0;
			}
		}
	}

	return addsize;
}

/**
 * The function `ftp_list_dir` reads up to 8 directory items and adds them to a list while skipping "."
 * and ".." entries.
 *
 * @param list The `list` parameter is a pointer to a character array where directory items will be
 * stored.
 * @param maxlistsize The `maxlistsize` parameter in the `ftp_list_dir` function represents the maximum
 * size of the list buffer that is passed to the function. It indicates the maximum number of bytes
 * that can be written to the `list` buffer by the function without exceeding its allocated memory.
 * @param listsize The `listsize` parameter in the `ftp_list_dir` function is a pointer to a `uint32_t`
 * variable. This parameter is used to store the size of the directory listing that is generated by the
 * function. The function calculates the size of the directory listing and updates the value pointed to
 *
 * @return The function `ftp_list_dir` returns a variable of type `ftp_result_t`, which is an
 * enumerated type representing the result of the FTP operation. The possible return values are
 * `E_FTP_RESULT_CONTINUE` and `E_FTP_RESULT_OK`.
 */
static ftp_result_t ftp_list_dir(char *list, uint32_t maxlistsize,
                                 uint32_t *listsize)
{
    uint next = 0;
    uint listcount = 0;
    ftp_result_t result = E_FTP_RESULT_CONTINUE;
    struct dirent *de;

    // read up to 8 directory items
    while (((maxlistsize - next) > 64) && (listcount < 8))
    {
        de = readdir(ftp_data.dp); // Read a directory item
        ESP_LOGI(FTP_TAG, "readdir de=%p", de);

        if (de == NULL)
        {
            result = E_FTP_RESULT_OK;
            break; // Break on error or end of dp
        }

        if (de->d_name[0] == '.' && de->d_name[1] == 0)
            continue; // Ignore . entry
        if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == 0)
            continue; // Ignore .. entry

        // add the entry to the list
        ESP_LOGI(FTP_TAG, "Add to dir list: %s", de->d_name);
        next += ftp_get_eplf_item((list + next), (maxlistsize - next), de);
        listcount++;
    }

    if (result == E_FTP_RESULT_OK)
    {
        ftp_close_files_dir();
    }

    *listsize = next;

    return result;
}

/**
 * The function ftp_close_cmd_data closes the command and data sockets and resets their values to -1.
 */
static void ftp_close_cmd_data(void)
{
    closesocket(ftp_data.c_sd);
    closesocket(ftp_data.d_sd);
    ftp_data.c_sd = -1;
    ftp_data.d_sd = -1;
    ftp_close_filesystem_on_error();
}

/**
 * The _ftp_reset function closes all connections and resets the FTP state variables.
 */
static void _ftp_reset(void)
{
    // close all connections and start all over again
    ESP_LOGW(FTP_TAG, "FTP RESET");
    closesocket(ftp_data.lc_sd);
    closesocket(ftp_data.ld_sd);

    ftp_data.lc_sd = -1;
    ftp_data.ld_sd = -1;

    ftp_close_cmd_data();

    ftp_data.e_open = E_FTP_NOTHING_OPEN;
    ftp_data.state = E_FTP_STE_START;
    ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
}

/**
 * The function `ftp_create_listening_socket` creates a listening socket for FTP data connections on a
 * specified port with a given backlog value.
 *
 * @param sd The `sd` parameter is a pointer to an `int32_t` variable that will store the socket
 * descriptor created by the `socket` function.
 * @param port The `port` parameter in the `ftp_create_listening_socket` function represents the port
 * number on which the socket will listen for incoming connections. It is of type `uint32_t`, which
 * means it is an unsigned 32-bit integer. The port number is used to uniquely identify different
 * network services
 * @param backlog The `backlog` parameter in the `ftp_create_listening_socket` function represents the
 * maximum length to which the queue of pending connections for the created socket may grow. It is used
 * when calling the `listen` function to specify the maximum number of pending connections that can be
 * queued up before they are
 *
 * @return The function `ftp_create_listening_socket` returns a boolean value. If the socket creation,
 * binding, and listening operations are successful, it returns `true`. Otherwise, it returns `false`.
 */
static bool ftp_create_listening_socket(int32_t *sd, uint32_t port, uint8_t backlog)
{
    struct sockaddr_in sServerAddress;
    int32_t _sd;
    int32_t result;

    // open a socket for ftp data listen
    *sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    _sd = *sd;

    if (_sd > 0)
    {
        // enable non-blocking mode
        uint32_t option = fcntl(_sd, F_GETFL, 0);
        option |= O_NONBLOCK;
        fcntl(_sd, F_SETFL, option);

        // enable address reusing
        option = 1;
        result = setsockopt(_sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

        // bind the socket to a port number
        sServerAddress.sin_family = AF_INET;
        sServerAddress.sin_addr.s_addr = INADDR_ANY;
        sServerAddress.sin_len = sizeof(sServerAddress);
        sServerAddress.sin_port = htons(port);

        result |= bind(_sd, (const struct sockaddr *)&sServerAddress, sizeof(sServerAddress));

        // start listening
        result |= listen(_sd, backlog);

        if (!result)
        {
            return true;
        }
        closesocket(*sd);
    }

    return false;
}

/**
 * The function `ftp_wait_for_connection` accepts a connection from a TCP client and returns the
 * result.
 *
 * @param l_sd The `l_sd` parameter in the `ftp_wait_for_connection` function represents the listening
 * socket descriptor. This descriptor is used to accept incoming connections from TCP clients.
 * @param n_sd The `n_sd` parameter in the `ftp_wait_for_connection` function is a pointer to an
 * `int32_t` variable where the function will store the file descriptor of the accepted connection if a
 * client connects successfully.
 * @param ip_addr The `ip_addr` parameter in the `ftp_wait_for_connection` function is a pointer to a
 * `uint32_t` variable where the IP address of the client will be stored if provided. The function
 * retrieves the client's IP address and saves it in the `ip_addr` variable if it is
 *
 * @return The function `ftp_wait_for_connection` returns an `ftp_result_t` enum value. It can return
 * one of the following values:
 * - `E_FTP_RESULT_CONTINUE` if there is no connection from a TCP client and the function should
 * continue.
 * - `E_FTP_RESULT_FAILED` if there was an error accepting the connection and the FTP state should be
 * reset.
 * - `E_FTP_RESULT
 */
static ftp_result_t ftp_wait_for_connection(int32_t l_sd, int32_t *n_sd, uint32_t *ip_addr)
{
    struct sockaddr_in sClientAddress;
    socklen_t in_addrSize;

    // accepts a connection from a TCP client, if there is any, otherwise returns EAGAIN
    *n_sd = accept(l_sd, (struct sockaddr *)&sClientAddress, (socklen_t *)&in_addrSize);
    int32_t _sd = *n_sd;

    if (_sd < 0)
    {
        if (errno == EAGAIN)
        {
            return E_FTP_RESULT_CONTINUE;
        }
        // error
        _ftp_reset();
        return E_FTP_RESULT_FAILED;
    }

    if (ip_addr)
    {
        // check on which network interface the client was connected and save the IP address
        struct sockaddr_in clientAddr, serverAddr;
        in_addrSize = sizeof(struct sockaddr_in);
        getpeername(_sd, (struct sockaddr *)&clientAddr, (socklen_t *)&in_addrSize);
        getsockname(_sd, (struct sockaddr *)&serverAddr, (socklen_t *)&in_addrSize);
        ESP_LOGI(FTP_TAG, "Client IP: 0x%08" PRIx32, clientAddr.sin_addr.s_addr);
        ESP_LOGI(FTP_TAG, "Server IP: 0x%08" PRIx32, serverAddr.sin_addr.s_addr);
        *ip_addr = serverAddr.sin_addr.s_addr;
    }

    // enable non-blocking mode if not data channel connection
    uint32_t option = fcntl(_sd, F_GETFL, 0);
    if (l_sd != ftp_data.ld_sd)
        option |= O_NONBLOCK;
    fcntl(_sd, F_SETFL, option);

    // client connected, so go on
    return E_FTP_RESULT_OK;
}

/**
 * The function `ftp_send_reply` sends a formatted FTP reply message with a specified status and
 * message.
 *
 * @param status The `status` parameter in the `ftp_send_reply` function is of type `uint32_t`, which
 * is an unsigned 32-bit integer. It is used to indicate the status code of the FTP reply message that
 * will be sent. Status codes are standard codes used in FTP communication to indicate the
 * @param message The `ftp_send_reply` function takes two parameters: `status` of type `uint32_t` and
 * `message` of type `char *`. The `message` parameter is a pointer to a character array that contains
 * the message to be sent as a reply. If the `message` parameter
 */
static void ftp_send_reply(uint32_t status, char *message)
{
    if (!message)
    {
        message = "";
    }

    snprintf((char *)ftp_cmd_buffer, 4, "%" PRIu32, status);
    strcat((char *)ftp_cmd_buffer, " ");
    strcat((char *)ftp_cmd_buffer, message);
    strcat((char *)ftp_cmd_buffer, "\r\n");

    int32_t timeout = 200;
    ftp_result_t result;
    size_t size = strlen((char *)ftp_cmd_buffer);

    ESP_LOGI(FTP_TAG, "Send reply: [%.*s]", size - 2, ftp_cmd_buffer);
    vTaskDelay(1);

    while (1)
    {
        result = send(ftp_data.c_sd, ftp_cmd_buffer, size, 0);
        if (result == size)
        {
            if (status == 221)
            {
                closesocket(ftp_data.d_sd);
                ftp_data.d_sd = -1;
                closesocket(ftp_data.ld_sd);
                ftp_data.ld_sd = -1;
                closesocket(ftp_data.c_sd);
                ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
                ftp_close_filesystem_on_error();
            }
            else if (status == 426 || status == 451 || status == 550)
            {
                closesocket(ftp_data.d_sd);
                ftp_data.d_sd = -1;
                ftp_close_filesystem_on_error();
            }
            vTaskDelay(1);
            ESP_LOGI(FTP_TAG, "Send reply: OK (%u)", size);
            break;
        }
        else
        {
            vTaskDelay(1);
            if ((timeout <= 0) || (errno != EAGAIN))
            {
                // error
                _ftp_reset();
                ESP_LOGW(FTP_TAG, "Error sending command reply.");
                break;
            }
        }
        timeout -= portTICK_PERIOD_MS;
    }
}

/**
 * The function `ftp_send_list` sends a list of data with a specified size over FTP, handling timeouts
 * and errors.
 *
 * @param datasize The `datasize` parameter in the `ftp_send_list` function represents the size of the
 * data to be sent over FTP (File Transfer Protocol). It is of type `uint32_t`, which is an unsigned
 * 32-bit integer. This parameter specifies the amount of data that needs to be sent
 */
static void ftp_send_list(uint32_t datasize)
{
    int32_t timeout = 200;
    ftp_result_t result;

    ESP_LOGI(FTP_TAG, "Send list data: (%" PRIu32 ")", datasize);
    vTaskDelay(1);

    while (1)
    {
        result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
        if (result == datasize)
        {
            vTaskDelay(1);
            ESP_LOGI(FTP_TAG, "Send OK");
            break;
        }
        else
        {
            vTaskDelay(1);
            if ((timeout <= 0) || (errno != EAGAIN))
            {
                // error
                _ftp_reset();
                ESP_LOGW(FTP_TAG, "Error sending list data.");
                break;
            }
        }

        timeout -= portTICK_PERIOD_MS;
    }
}

/**
 * The function `ftp_send_file_data` sends file data over FTP with error handling and timeout.
 *
 * @param datasize The `datasize` parameter in the `ftp_send_file_data` function represents the size of
 * the data to be sent over FTP (File Transfer Protocol). It is of type `uint32_t`, which is an
 * unsigned 32-bit integer. This parameter specifies the amount of data to be sent in
 */
static void ftp_send_file_data(uint32_t datasize)
{
    ftp_result_t result;
    uint32_t timeout = 200;

    ESP_LOGI(FTP_TAG, "Send file data: (%" PRIu32 ")", datasize);
    vTaskDelay(1);

    while (1)
    {
        result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
        if (result == datasize)
        {
            vTaskDelay(1);
            ESP_LOGI(FTP_TAG, "Send OK");
            break;
        }
        else
        {
            vTaskDelay(1);
            if ((timeout <= 0) || (errno != EAGAIN))
            {
                // error
                _ftp_reset();
                ESP_LOGW(FTP_TAG, "Error sending file data.");
                break;
            }
        }
        timeout -= portTICK_PERIOD_MS;
    }
}

/**
 * The function `ftp_recv_non_blocking` receives data from a socket in a non-blocking manner and
 * returns a result indicating success, failure, or the need to continue.
 *
 * @param sd The `sd` parameter is the socket descriptor representing the connection over which data is
 * being received.
 * @param buff The `buff` parameter in the `ftp_recv_non_blocking` function is a pointer to a buffer
 * where the received data will be stored. When the function successfully receives data, it will be
 * written into this buffer.
 * @param Maxlen The `Maxlen` parameter in the `ftp_recv_non_blocking` function represents the maximum
 * length of the buffer `buff` that can be filled with received data. It specifies the maximum number
 * of bytes that can be read from the socket descriptor `sd` and stored in the buffer `buff`.
 * @param rxLen The `rxLen` parameter in the `ftp_recv_non_blocking` function is a pointer to an
 * integer where the function will store the number of bytes received from the socket `sd`.
 *
 * @return The function `ftp_recv_non_blocking` returns one of the following values:
 * - `E_FTP_RESULT_OK` if data was successfully received.
 * - `E_FTP_RESULT_FAILED` if an error occurred other than `EAGAIN`.
 * - `E_FTP_RESULT_CONTINUE` if the operation needs to continue due to `EAGAIN` (indicating that there
 * is no data available for reading at the
 */
static ftp_result_t ftp_recv_non_blocking(int32_t sd, void *buff, int32_t Maxlen, int32_t *rxLen)
{
    if (sd < 0)
        return E_FTP_RESULT_FAILED;

    *rxLen = recv(sd, buff, Maxlen, 0);
    if (*rxLen > 0)
        return E_FTP_RESULT_OK;
    else if (errno != EAGAIN)
        return E_FTP_RESULT_FAILED;

    return E_FTP_RESULT_CONTINUE;
}

// ******** Directory Function **************************

/**
 * The function `ftp_open_child` updates the current working directory path based on the input
 * directory provided.
 *
 * @param pwd The `pwd` parameter in the `ftp_open_child` function represents the current working
 * directory path. It is a character array that holds the current directory path before navigating to
 * the new directory specified by the `dir` parameter.
 * @param dir The `dir` parameter in the `ftp_open_child` function represents the directory or file
 * name that needs to be opened or accessed. It can be either an absolute path (starting with '/') or a
 * relative path.
 */
static void ftp_open_child(char *pwd, char *dir)
{
    ESP_LOGI(FTP_TAG, "open_child: %s + %s", pwd, dir);
    if (strlen(dir) > 0)
    {
        if (dir[0] == '/')
        {
            // ** absolute path
            strcpy(pwd, dir);
        }
        else
        {
            // ** relative path
            // add trailing '/' if needed
            if ((strlen(pwd) > 1) && (pwd[strlen(pwd) - 1] != '/') && (dir[0] != '/'))
                strcat(pwd, "/");
            // append directory/file name
            strcat(pwd, dir);
        }
    }

    ESP_LOGI(FTP_TAG, "open_child, New pwd: %s", pwd);
}

/**
 * The function `ftp_close_child` closes the child directory and adjusts the path accordingly.
 *
 * @param pwd The function `ftp_close_child` takes a parameter `pwd`, which is a pointer to a character
 * array (string) representing the current working directory path. The function manipulates this path
 * based on certain conditions and updates the `pwd` string accordingly.
 */
static void ftp_close_child(char *pwd)
{
    ESP_LOGI(FTP_TAG, "close_child: %s", pwd);
    uint len = strlen(pwd);
    if (pwd[len - 1] == '/')
    {
        pwd[len - 1] = '\0';
        len--;
        if ((len == 0) ||
            (strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) ||
            (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0))
        {
            strcpy(pwd, "/");
        }
    }
    else
    {
        while (len)
        {
            if (pwd[len - 1] == '/')
            {
                pwd[len - 1] = '\0';
                len--;
                break;
            }
            len--;
        }

        if (len == 0)
        {
            strcpy(pwd, "/");
        }
        else if ((strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) ||
                 (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0))
        {
            strcat(pwd, "/");
        }
    }

    ESP_LOGI(FTP_TAG, "close_child, New pwd: %s", pwd);
}

/**
 * The function `remove_fname_from_path` removes a specified filename from a given path string.
 *
 * @param pwd The `pwd` parameter is a string representing a file path.
 * @param fname The `fname` parameter in the `remove_fname_from_path` function is a pointer to a
 * character array that represents the file name to be removed from the `pwd` (path) string.
 *
 * @return If the length of the `fname` string is 0 or if the `fname` string is not found within the
 * `pwd` string, then the function will return without making any changes.
 */
static void remove_fname_from_path(char *pwd, char *fname)
{
    ESP_LOGI(FTP_TAG, "remove_fname_from_path: %s - %s", pwd, fname);
    if (strlen(fname) == 0)
        return;
    char *xpwd = strstr(pwd, fname);
    if (xpwd == NULL)
        return;

    xpwd[0] = '\0';

    ESP_LOGI(FTP_TAG, "remove_fname_from_path: New pwd: %s", pwd);
}

// ******** Param functions **************************

/**
 * The function `ftp_pop_param` extracts a parameter from a string, stopping based on specified
 * conditions.
 *
 * @param str The `str` parameter is a pointer to a pointer to a character (char **). This function
 * takes a string as input and modifies it based on the specified conditions.
 * @param param The `param` parameter in the `ftp_pop_param` function is a character array where the
 * extracted characters from the input string will be stored.
 * @param stop_on_space The `stop_on_space` parameter is a boolean flag that determines whether the
 * function should stop when encountering a space character in the input string. If `stop_on_space` is
 * set to true, the function will break out of the loop when it encounters a space character. If it is
 * set to false
 * @param stop_on_newline The `stop_on_newline` parameter in the `ftp_pop_param` function determines
 * whether the function should stop reading characters when it encounters a newline character (`\n`) or
 * continue reading past it. If `stop_on_newline` is set to `true`, the function will stop reading when
 * it
 */
static void ftp_pop_param(char **str, char *param, bool stop_on_space, bool stop_on_newline)
{
    char lastc = '\0';
    while (**str != '\0')
    {
        if (stop_on_space && (**str == ' '))
            break;
        if ((**str == '\r') || (**str == '\n'))
        {
            if (!stop_on_newline)
            {
                (*str)++;
                continue;
            }
            else
                break;
        }
        if ((**str == '/') && (lastc == '/'))
        {
            (*str)++;
            continue;
        }
        lastc = **str;
        *param++ = **str;
        (*str)++;
    }
    *param = '\0';
}

/**
 * The function `ftp_pop_command` parses a command string, converts it to uppercase, and checks if it
 * matches any command in a table, returning the index if found.
 *
 * @param str The `ftp_pop_command` function takes a pointer to a pointer to a character array (`char
 * **str`) as a parameter. This pointer is used to extract the command string from the input. The
 * function then processes this command string to determine the corresponding FTP command index.
 *
 * @return The function `ftp_pop_command` returns an `ftp_cmd_index_t` value, which is an enumeration
 * representing the index of the FTP command found in the `ftp_cmd_table`. If the command is found in
 * the table, the function returns the index of that command. If the command is not found or not
 * supported, it returns the value `E_FTP_CMD_NOT_SUPPORTED`.
 */
static ftp_cmd_index_t ftp_pop_command(char **str)
{
    char _cmd[FTP_CMD_SIZE_MAX];
    ftp_pop_param(str, _cmd, true, true);
    stoupper(_cmd);
    for (ftp_cmd_index_t i = 0; i < E_FTP_NUM_FTP_CMDS; i++)
    {
        if (!strcmp(_cmd, ftp_cmd_table[i].cmd))
        {
            // move one step further to skip the space
            (*str)++;
            return i;
        }
    }
    return E_FTP_CMD_NOT_SUPPORTED;
}

/**
 * The function `ftp_get_param_and_open_child` retrieves a parameter, opens a child connection, and
 * sets a flag to close the child connection.
 *
 * @param bufptr In the provided code snippet, the `bufptr` parameter is a pointer to a pointer to a
 * character array (`char **bufptr`). This function `ftp_get_param_and_open_child` is responsible for
 * retrieving a parameter using `ftp_pop_param`, opening a child using `ftp_open_child`, and
 */
static void ftp_get_param_and_open_child(char **bufptr)
{
    ftp_pop_param(bufptr, ftp_scratch_buffer, false, false);
    ftp_open_child(ftp_path, ftp_scratch_buffer);
    ftp_data.closechild = true;
}

// ******** Ftp command processing **************************

/**
 * The function `ftp_process_cmd` processes FTP commands received from a client, executing various FTP
 * commands such as changing directories, listing files, transferring files, and handling user
 * authentication.
 *
 * @return The function `ftp_process_cmd` does not explicitly return a value. It performs various
 * operations based on the received FTP command, sends replies, and handles different cases within a
 * switch statement. The function does not have a return statement at the end, so it implicitly returns
 * void.
 */
static void ftp_process_cmd(void)
{
    int32_t len;
    char *bufptr = (char *)ftp_cmd_buffer;
    ftp_result_t result;
    struct stat buf;
    int res;

    memset(bufptr, 0, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX);
    ftp_data.closechild = false;

    // use the reply buffer to receive new commands
    result = ftp_recv_non_blocking(ftp_data.c_sd, ftp_cmd_buffer, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX, &len);
    if (result == E_FTP_RESULT_OK)
    {
        ftp_cmd_buffer[len] = '\0';
        // bufptr is moved as commands are being popped
        ftp_cmd_index_t cmd = ftp_pop_command(&bufptr);
        if (!ftp_data.loggin.passvalid &&
            ((cmd != E_FTP_CMD_USER) && (cmd != E_FTP_CMD_PASS) && (cmd != E_FTP_CMD_QUIT) && (cmd != E_FTP_CMD_FEAT) && (cmd != E_FTP_CMD_AUTH)))
        {
            ftp_send_reply(332, NULL);
            return;
        }
        if ((cmd >= 0) && (cmd < E_FTP_NUM_FTP_CMDS))
        {
            ESP_LOGI(FTP_TAG, "CMD: %s", ftp_cmd_table[cmd].cmd);
        }
        else
        {
            ESP_LOGI(FTP_TAG, "CMD: %d", cmd);
        }
        char fullname[128];
        char fullname2[128];
        strcpy(fullname, MOUNT_POINT);
        strcpy(fullname2, MOUNT_POINT);

        switch (cmd)
        {
        case E_FTP_CMD_FEAT:
            ftp_send_reply(502, "no-features");
            break;
        case E_FTP_CMD_AUTH:
            ftp_send_reply(504, "not-supported");
            break;
        case E_FTP_CMD_SYST:
            ftp_send_reply(215, "UNIX Type: L8");
            break;
        case E_FTP_CMD_CDUP:
            ftp_close_child(ftp_path);
            ftp_send_reply(250, NULL);
            break;
        case E_FTP_CMD_CWD:
            ftp_pop_param(&bufptr, ftp_scratch_buffer, false, false);

            if (strlen(ftp_scratch_buffer) > 0)
            {
                if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '\0'))
                {
                    ftp_data.dp = NULL;
                    ftp_send_reply(250, NULL);
                    break;
                }
                if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '.') && (ftp_scratch_buffer[2] == '\0'))
                {
                    ftp_close_child(ftp_path);
                    ftp_send_reply(250, NULL);
                    break;
                }
                else
                    ftp_open_child(ftp_path, ftp_scratch_buffer);
            }

            if ((ftp_path[0] == '/') && (ftp_path[1] == '\0'))
            {
                ftp_data.dp = NULL;
                ftp_send_reply(250, NULL);
            }
            else
            {
                strcat(fullname, ftp_path);
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_CWD fullname=[%s]", fullname);
                // ftp_data.dp = opendir(ftp_path);
                ftp_data.dp = opendir(fullname);
                if (ftp_data.dp != NULL)
                {
                    closedir(ftp_data.dp);
                    ftp_data.dp = NULL;
                    ftp_send_reply(250, NULL);
                }
                else
                {
                    ftp_close_child(ftp_path);
                    ftp_send_reply(550, NULL);
                }
            }
            break;
        case E_FTP_CMD_PWD:
        case E_FTP_CMD_XPWD:
        {
            char lpath[128];
            strcpy(lpath, ftp_path);
            ftp_send_reply(257, lpath);
        }
        break;
        case E_FTP_CMD_SIZE:
        {
            ftp_get_param_and_open_child(&bufptr);
            strcat(fullname, ftp_path);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_SIZE fullname=[%s]", fullname);
            // int res = stat(ftp_path, &buf);
            int res = stat(fullname, &buf);
            if (res == 0)
            {
                // send the file size
                snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%" PRIu32, (uint32_t)buf.st_size);
                ftp_send_reply(213, (char *)ftp_data.dBuffer);
            }
            else
            {
                ftp_send_reply(550, NULL);
            }
        }
        break;
        case E_FTP_CMD_MDTM:
            ftp_get_param_and_open_child(&bufptr);
            strcat(fullname, ftp_path);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_MDTM fullname=[%s]", fullname);
            res = stat(fullname, &buf);
            if (res == 0)
            {
                time_t time = buf.st_mtime;
                struct tm *ptm = localtime(&time);
                strftime((char *)ftp_data.dBuffer, ftp_buff_size, "%Y%m%d%H%M%S", ptm);
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_MDTM ftp_data.dBuffer=[%s]", ftp_data.dBuffer);
                ftp_send_reply(213, (char *)ftp_data.dBuffer);
            }
            else
            {
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_TYPE:
            ftp_send_reply(200, NULL);
            break;
        case E_FTP_CMD_USER:
            ftp_pop_param(&bufptr, ftp_scratch_buffer, true, true);
            if (!memcmp(ftp_scratch_buffer, ftp_user, MAX(strlen(ftp_scratch_buffer), strlen(ftp_user))))
            {
                ftp_data.loggin.uservalid = true && (strlen(ftp_user) == strlen(ftp_scratch_buffer));
            }
            ftp_send_reply(331, NULL);
            break;
        case E_FTP_CMD_PASS:
            ftp_pop_param(&bufptr, ftp_scratch_buffer, true, true);
            if (!memcmp(ftp_scratch_buffer, ftp_pass, MAX(strlen(ftp_scratch_buffer), strlen(ftp_pass))) &&
                ftp_data.loggin.uservalid)
            {
                ftp_data.loggin.passvalid = true && (strlen(ftp_pass) == strlen(ftp_scratch_buffer));
                if (ftp_data.loggin.passvalid)
                {
                    ftp_send_reply(230, NULL);
                    break;
                }
            }
            ftp_send_reply(530, NULL);
            break;
        case E_FTP_CMD_PASV:
        {
            // some servers (e.g. google chrome) send PASV several times very quickly
            closesocket(ftp_data.d_sd);
            ftp_data.d_sd = -1;
            ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
            bool socketcreated = true;
            if (ftp_data.ld_sd < 0)
            {
                socketcreated = ftp_create_listening_socket(&ftp_data.ld_sd, FTP_PASIVE_DATA_PORT, FTP_DATA_CLIENTS_MAX - 1);
            }
            if (socketcreated)
            {
                uint8_t *pip = (uint8_t *)&ftp_data.ip_addr;
                ftp_data.dtimeout = 0;
                snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "(%u,%u,%u,%u,%u,%u)",
                         pip[0], pip[1], pip[2], pip[3], (FTP_PASIVE_DATA_PORT >> 8), (FTP_PASIVE_DATA_PORT & 0xFF));
                ftp_data.substate = E_FTP_STE_SUB_LISTEN_FOR_DATA;
                ESP_LOGI(FTP_TAG, "Data socket created");
                ftp_send_reply(227, (char *)ftp_data.dBuffer);
            }
            else
            {
                ESP_LOGW(FTP_TAG, "Error creating data socket");
                ftp_send_reply(425, NULL);
            }
        }
        break;
        case E_FTP_CMD_LIST:
        case E_FTP_CMD_NLST:
            ftp_get_param_and_open_child(&bufptr);
            if (cmd == E_FTP_CMD_LIST)
                ftp_nlist = 0;
            else
                ftp_nlist = 1;
            if (ftp_open_dir_for_listing(ftp_path) == E_FTP_RESULT_CONTINUE)
            {
                ftp_data.state = E_FTP_STE_CONTINUE_LISTING;
                ftp_send_reply(150, NULL);
            }
            else
                ftp_send_reply(550, NULL);
            break;
        case E_FTP_CMD_RETR:
            ftp_data.total = 0;
            ftp_data.time = 0;
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                if (ftp_open_file(ftp_path, "rb"))
                {
                    ftp_data.state = E_FTP_STE_CONTINUE_FILE_TX;
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(150, NULL);
                }
                else
                {
                    ftp_data.state = E_FTP_STE_END_TRANSFER;
                    ftp_send_reply(550, NULL);
                }
            }
            else
            {
                ftp_data.state = E_FTP_STE_END_TRANSFER;
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_APPE:
            ftp_data.total = 0;
            ftp_data.time = 0;
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                if (ftp_open_file(ftp_path, "ab"))
                {
                    ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(150, NULL);
                }
                else
                {
                    ftp_data.state = E_FTP_STE_END_TRANSFER;
                    ftp_send_reply(550, NULL);
                }
            }
            else
            {
                ftp_data.state = E_FTP_STE_END_TRANSFER;
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_STOR:
            ftp_data.total = 0;
            ftp_data.time = 0;
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_STOR ftp_path=[%s]", ftp_path);
                if (ftp_open_file(ftp_path, "wb"))
                {
                    ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(150, NULL);
                }
                else
                {
                    ftp_data.state = E_FTP_STE_END_TRANSFER;
                    ftp_send_reply(550, NULL);
                }
            }
            else
            {
                ftp_data.state = E_FTP_STE_END_TRANSFER;
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_DELE:
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_DELE ftp_path=[%s]", ftp_path);

                strcat(fullname, ftp_path);
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_DELE fullname=[%s]", fullname);

                // if (unlink(ftp_path) == 0) {
                if (unlink(fullname) == 0)
                {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(250, NULL);
                }
                else
                    ftp_send_reply(550, NULL);
            }
            else
                ftp_send_reply(250, NULL);
            break;
        case E_FTP_CMD_RMD:
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_RMD ftp_path=[%s]", ftp_path);

                strcat(fullname, ftp_path);
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

                // if (rmdir(ftp_path) == 0) {
                if (rmdir(fullname) == 0)
                {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(250, NULL);
                }
                else
                    ftp_send_reply(550, NULL);
            }
            else
                ftp_send_reply(250, NULL);
            break;
        case E_FTP_CMD_MKD:
            ftp_get_param_and_open_child(&bufptr);
            if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path) - 1] != '/'))
            {
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD ftp_path=[%s]", ftp_path);

                strcat(fullname, ftp_path);
                ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

                if (mkdir(fullname, 0755) == 0)
                {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    ftp_send_reply(250, NULL);
                }
                else
                    ftp_send_reply(550, NULL);
            }
            else
                ftp_send_reply(250, NULL);
            break;
        case E_FTP_CMD_RNFR:
            ftp_get_param_and_open_child(&bufptr);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNFR ftp_path=[%s]", ftp_path);

            strcat(fullname, ftp_path);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

            res = stat(fullname, &buf);
            if (res == 0)
            {
                ftp_send_reply(350, NULL);
                // save the path of the file to rename
                strcpy((char *)ftp_data.dBuffer, ftp_path);
            }
            else
            {
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_RNTO:
            ftp_get_param_and_open_child(&bufptr);
            // the path of the file to rename was saved in the data buffer
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO ftp_path=[%s], ftp_data.dBuffer=[%s]", ftp_path, (char *)ftp_data.dBuffer);
            strcat(fullname, (char *)ftp_data.dBuffer);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO fullname=[%s]", fullname);
            strcat(fullname2, ftp_path);
            ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO fullname2=[%s]", fullname2);

            // if (rename((char *)ftp_data.dBuffer, ftp_path) == 0) {
            if (rename(fullname, fullname2) == 0)
            {
                ftp_send_reply(250, NULL);
            }
            else
            {
                ftp_send_reply(550, NULL);
            }
            break;
        case E_FTP_CMD_NOOP:
            ftp_send_reply(200, NULL);
            break;
        case E_FTP_CMD_QUIT:
            ftp_send_reply(221, NULL);
            break;
        default:
            // command not implemented
            ftp_send_reply(502, NULL);
            break;
        }

        if (ftp_data.closechild)
        {
            remove_fname_from_path(ftp_path, ftp_scratch_buffer);
        }
    }
    else if (result == E_FTP_RESULT_CONTINUE)
    {
        if (ftp_data.ctimeout > ftp_timeout)
        {
            ftp_send_reply(221, NULL);
            ESP_LOGW(FTP_TAG, "Connection timeout");
        }
    }
    else
    {
        ftp_close_cmd_data();
    }
}

/**
 * The function `ftp_wait_for_enabled` checks if the telnet service has been enabled and updates the
 * FTP state accordingly.
 */
static void ftp_wait_for_enabled(void)
{
    // Check if the telnet service has been enabled
    if (ftp_data.enabled)
    {
        ftp_data.state = E_FTP_STE_START;
    }
}