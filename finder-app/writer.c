/*
    Writer program:
    Brief: This software is used to open an existing file and write a string passed in its second
    passed arguments. The program shall create the file if not created and append if exist at the end of it

    Arguments:  
        1) Path to the directory containing the file 
        2) string to be added to the file pointed to by the first argument
 */
/*--------------------------------- Private includes ---------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <syslog.h>

/*--------------------------------- Private definitions ---------------------------------  */
#define SUCCESS                                 0
#define CUSTOM_ERROR                            EPERM
#define FILE_DESCRIPTOR_NOT_EXIST               -1
#define ERROR_COULD_NOT_WRITE_CORRECTLY         -1
#define ERROR_COULD_NOT_CLOSE_FILE              -1
/* Number of arguments including the name of the executable */
#define WRITER_EXPECTED_NUM_ARGUMENTS           3

#define INDEX_OF_FILE_PATH                      1
#define INDEX_OF_STRING                         2

#define NULL_TERMINATOR_SIZE                    1

/*------------------------------ Private MACRO Functions ------------------------------  */
#define SET_ERROR_STATUS_AND_ERROR_MSG(err_status, msg) status = err_status;  \
                                                        if (errno == 0)       \
                                                        errno = status;       \
                                                        syslog(LOG_ERR, msg); \
                                                        goto func_exit;

/*--------------------------------- Private Functions ---------------------------------  */
/**
 * @brief extract the directory path and file name from a passed complete file path 
 * 
 * @param file_path     [IN]  Pointer to the complete file path
 * @param ptr_dir_path  [OUT] Pointer to pointer to directory path 
 * @param ptr_file_name [OUT] Pointer to pointer to file name
 * 
 * @return status indicating the correctness of extraction
 * 
 * @note output both pointers shall be allocated in the function and shall be freed by the caller
 * @note This function will not work well with non-unix based pathes ONE NEED TO CHANGE THE DELIMITER
 */
static int extract_dir_path_from_file_path(const char * file_path, char ** ptr_dir_path, char** ptr_file_name)
{
    /*--------------------------------- Parameters ---------------------------------*/
    char* ptr_last_delimiter;
    int dir_path_len;
    int file_name_len;
    int status = SUCCESS;
    char * dir_path;
    char * file_name;
    /*--------------------------------- Checkers ---------------------------------*/
    ptr_last_delimiter = strrchr(file_path, '/');
    if (ptr_last_delimiter == NULL)
    {
        status = CUSTOM_ERROR;
        goto func_exit;
    }
    /*--------------------------------- Body ---------------------------------*/
    /* extract the directory path */
    dir_path_len =  ptr_last_delimiter - file_path;
    dir_path  = (char*) malloc(dir_path_len);
    strncpy(dir_path, file_path, dir_path_len);
    dir_path[dir_path_len] = '\0';
    /* extract the file name */
    file_name_len =  strlen(ptr_last_delimiter); /* this will include an extra char which we could consider it as null terminator */
    file_name  = (char*) malloc(file_name_len);
    strcpy(file_name, ++ptr_last_delimiter);

    /* Assign the outputs of the function */
    *ptr_dir_path  = dir_path;
    *ptr_file_name = file_name; 

func_exit:
    return status;
}

/**
 * @brief clean the allocated system resources
 * 
 * @param dir_path          [IN] Pointer to pointer to directory path allocated while calling extract_dir_path_from_file_path
 * @param file_name         [IN] Pointer to pointer to file name allocated while calling extract_dir_path_from_file_path
 * @param file_descriptor   [IN] Openned file descriptor
 * 
 * @return None
 */
static void app_clean(char* dir_path, char* file_name, int file_descriptor)
{
    if (dir_path != NULL)
    {
        free((void*)dir_path);
    }
    if (file_name != NULL)
    {
        free((void*)file_name);
    }
    if (file_descriptor != FILE_DESCRIPTOR_NOT_EXIST)
    {
        close(file_descriptor);
    }
    closelog();
}
/*--------------------------------- Main Function ---------------------------------  */
int main(int argc, char** argv)
{
    /*--------------------------------- Parameters ---------------------------------*/
    DIR * dir_handler;
    ssize_t num_written_octets;
    int file_descriptor     = FILE_DESCRIPTOR_NOT_EXIST;
    int status              = SUCCESS;
    char* str_to_add        = NULL;
    char* dir_path          = NULL;
    char* file_path         = NULL;
    char* file_name         = NULL;
    /*--------------------------------- Logger Enable ---------------------------------*/
    openlog(NULL, 0, LOG_USER);
    /*--------------------------------- Checkers ---------------------------------*/

    /* Check the number of arguments passed to the program */
    if (argc != WRITER_EXPECTED_NUM_ARGUMENTS)
    {
        SET_ERROR_STATUS_AND_ERROR_MSG(CUSTOM_ERROR, "Error number of arguments isn't correct");        
    }

    /* Assign argv[2] */
    file_path  = argv[INDEX_OF_FILE_PATH];
    str_to_add = argv[INDEX_OF_STRING];

    /* Extract directory path from the absolute file passed to check its existence */
    status = extract_dir_path_from_file_path(argv[INDEX_OF_FILE_PATH], &dir_path, &file_name);
    if(status != SUCCESS)
    {
        SET_ERROR_STATUS_AND_ERROR_MSG(CUSTOM_ERROR, "Error in file path argument");
    }
    /* Try to open the directory to see of exist */
    dir_handler = opendir(dir_path);
    if (NULL == dir_handler)
    {
        SET_ERROR_STATUS_AND_ERROR_MSG(CUSTOM_ERROR, "Error opening the directory");
    }
    /*--------------------------------- FILE IO ---------------------------------*/
    file_descriptor = open(file_path, O_CREAT|O_RDWR|O_APPEND, 
                                S_IRWXU | S_IRWXG | S_IRWXO);
    if (FILE_DESCRIPTOR_NOT_EXIST == file_descriptor)
    {
        SET_ERROR_STATUS_AND_ERROR_MSG(CUSTOM_ERROR, "Error opening/Creating the file");
    }
    num_written_octets = write(file_descriptor,str_to_add, strlen(str_to_add));
    if (ERROR_COULD_NOT_WRITE_CORRECTLY == num_written_octets)
    {
       SET_ERROR_STATUS_AND_ERROR_MSG(CUSTOM_ERROR, "Error Writing in the file"); 
    }
    syslog(LOG_DEBUG, "Writing %s to %s", str_to_add, file_name);
    
func_exit:
    /* Destruction */
    app_clean(dir_path, file_name, file_descriptor);
    
    return status;
}