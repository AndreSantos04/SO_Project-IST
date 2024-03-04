#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
/*
Verificar se é suposto as reservas poderem continuar a ser feitas com o mesmo reservation id depois de uma reserva desse evento ter falhado
*/

typedef struct{
    threads_data* data;
    int thread_id;
}thread_function_args;

size_t MAX_THREADS = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void* child_code(char *dirpath,struct dirent *entry);

void* thread_function(void* args);

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  if (argc > 4) {
    char *endptr;
    unsigned long int delay = strtoul(argv[4], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  char *dirpath = argv[1];
  int const MAX_PROC = atoi(argv[2]);
  MAX_THREADS = (size_t)atoi(argv[3]);
  DIR *dir = opendir(dirpath);

  if(dir == NULL) {
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }
  
  int pid;
  struct dirent *entry;
  int num_proc = 0;
  while ((entry = readdir(dir)) != NULL) {
    if(strstr(entry->d_name, ".jobs") == NULL){
      continue;
    }
    
    if(num_proc < MAX_PROC){
      pid = fork();
      if (pid == -1) {
        fprintf(stderr, "Error creating child process\n");
      }
      else {
        num_proc++;
      }
    }
    else{
      int status;
      pid = wait(&status);
      if(pid == -1){
        fprintf(stderr, "Error waiting for child process\n");
        continue;
      }
      if(WIFEXITED(status)){
        printf("Child process %d exited with status %d\n", pid, WEXITSTATUS(status));
      } 
      pid = fork();
      if (pid == -1) {
        fprintf(stderr, "Error creating child process\n");
      }
    }

    if(pid == 0){
      child_code(dirpath, entry);
      break;
    }
  }
    
  if(pid > 0){
    for(int i = 0; i < num_proc; i++){
      int status;
      pid = wait(&status);
      if(pid == -1){
        fprintf(stderr, "Error waiting for child process\n");
        continue;
      }
      if(WIFEXITED(status)){
        printf("Child process %d exited with status %d\n", pid, WEXITSTATUS(status));
      } 
    }
  }
  ems_terminate();
  if(closedir(dir) == -1){
    fprintf(stderr, "Failed to close directory\n");
    return 1;
  }
  exit(0);
  
}

void* child_code(char *dirpath,struct dirent *entry) {
  
  int openFlags = O_CREAT | O_WRONLY | O_TRUNC;
  mode_t filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  threads_data* data = (threads_data *)malloc(sizeof(threads_data));
  if(data == NULL){
    fprintf(stderr, "Failed to allocating memory to the data\n");
    return NULL;
  }
  data->threads = (thread_info*) malloc(sizeof(thread_info)*MAX_THREADS);
  if(data->threads == NULL){
    fprintf(stderr, "Failed to allocating memory to the threads\n");
    free(data);
    return NULL;
  }

  char filename[strlen(entry->d_name)+1];
  strcpy(filename, entry->d_name);
  
  char *filepath = (char *)malloc(strlen(dirpath) + strlen("/") + strlen(filename)+1);
  if(filepath == NULL){
    fprintf(stderr, "Failed to allocating memory to the filepath\n");
    return NULL;
  }
  strcpy(filepath, dirpath);
  strcat(filepath, "/");
  strcat(filepath, filename);

  data->fdR = open(filepath, O_RDONLY);
  if(data->fdR  == -1) {
    free(filepath);
    free(data);
    fprintf(stderr, "Failed to open file\n");
    return NULL;
  }

  char outputfile[strlen(filepath)+1];
  strcpy(outputfile, filepath);
  char *extension = strrchr(outputfile, '.');
  strcpy(extension, ".out");


  data->fdW = open(outputfile, openFlags, filePerms);
  if(data->fdW  == -1){
    fprintf(stderr, "Error opening file\n");
    free(filepath);
    free(data);
    return NULL;
  }

  thread_function_args* args = (thread_function_args *)malloc(sizeof(thread_function_args) * MAX_THREADS);
  if(args == NULL){
    fprintf(stderr, "Failed to allocating memory to the args\n");
    free(filepath);
    free(data);
    return NULL;
  }
  for(size_t i = 0; i < MAX_THREADS; i++){
    data->threads[i].delay = 0;
    data->threads[i].waited = 0;
    data->threads[i].mutex_wait_thread = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    args[i].data = data;
    args[i].thread_id = (int)i;
  }
  while(1){
    int foundBarrier = 0;
    data->barrier = 0;
    thread_function_args* temp = args;
    for(size_t i = 0; i < MAX_THREADS; i++){
      if(pthread_create(&data->threads[i].thread, NULL, thread_function, (void *) temp) != 0){
        fprintf(stderr, "Failed to create thread\n");
      }
      temp++;
    }


    for(size_t i = 0; i < MAX_THREADS; i++){
      int* return_value;
      if(pthread_join(data->threads[i].thread, (void**) &return_value) != 0){
        fprintf(stderr, "Failed to join thread\n");
        return NULL;
      }
      if(return_value == NULL)
        continue;
      if(*return_value){
        foundBarrier = 1;
      }
      free(return_value);
    }
    if(!foundBarrier){
      break;
    }

  }
  
  if(close(data->fdR) == -1 || close(data->fdW) == -1){
    fprintf(stderr, "Failed to close file\n");
  }
  free(data->threads);
  free(data);
  free(filepath);
  free(args);
  return NULL;
}

void* thread_function(void* _args){
  int* return_value = (int *)malloc(sizeof(int));
  if(return_value == NULL){
    fprintf(stderr, "Failed to allocating memory to the return value\n");
    return NULL;
  }
  *return_value = 0;
  thread_function_args* args = (thread_function_args *) _args;
  if(args == NULL){
    fprintf(stderr, "Error passing arguments to thread\n");
    *return_value = -1;
    return (void*)return_value;
  }
  threads_data* data = args->data;
  int id = args->thread_id;
  int fdR = data->fdR;
  int fdW = data->fdW;

  unsigned int event_id, delay, thread_id;
  size_t num_rows, num_columns, num_coords;
  Coordinate coords[MAX_RESERVATION_SIZE];

  while (1){
    
    pthread_mutex_lock(&data->threads[id].mutex_wait_thread);
  

    if(data->threads[id].delay != 0){

      delay = data->threads[id].delay;
      data->threads[id].delay = 0;
      pthread_mutex_unlock(&data->threads[id].mutex_wait_thread);
      ems_wait(delay);
      continue;
      
    }
    else
      pthread_mutex_unlock(&data->threads[id].mutex_wait_thread);
    
    pthread_mutex_lock(&mutex);
    if(data->barrier){
      *return_value = 1;
      pthread_mutex_unlock(&mutex);
      fprintf(stderr, "Thread reached barrier, didn't finish processing file\n");
      return (void*)return_value;
    }

    switch (get_next(fdR)) {
      case CMD_CREATE:
   
        if (parse_create(fdR, &event_id, &num_rows, &num_columns) != 0) {
          pthread_mutex_unlock(&mutex);
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        pthread_mutex_unlock(&mutex);
        
        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n"); 
        }
        break;
      
      case CMD_RESERVE:

        num_coords = parse_reserve(fdR, MAX_RESERVATION_SIZE, &event_id, coords);
        pthread_mutex_unlock(&mutex);
        
        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        qsort(coords, num_coords, sizeof(Coordinate), compareCoordinates);

        if (ems_reserve(event_id, num_coords, coords)) {
          fprintf(stderr, "Failed to reserve seats\n");
        }

        break;
      
      case CMD_SHOW:

        if (parse_show(fdR, &event_id) != 0) {
          pthread_mutex_unlock(&mutex);
          
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        pthread_mutex_unlock(&mutex);

        if (ems_show(event_id, fdW)) {
          fprintf(stderr, "Failed to show event\n");
        }
        
        break;

      case CMD_LIST_EVENTS:

        pthread_mutex_unlock(&mutex);
        if (ems_list_events(fdW)) {
          fprintf(stderr, "Failed to list events\n");
        }
        
        break;

      case CMD_WAIT:
  
        int parseWait = parse_wait(fdR, &delay, &thread_id);
        pthread_mutex_unlock(&mutex);
        if (parseWait == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
        }
        else if (parseWait == 0 && delay > 0) {
          for(size_t i = 0; i < MAX_THREADS; i++){
            pthread_mutex_lock(&data->threads[i].mutex_wait_thread);
            data->threads[i].delay += delay;
            pthread_mutex_unlock(&data->threads[i].mutex_wait_thread);
          }
        }
        else if (parseWait == 1 && delay > 0) {
          if (thread_id > MAX_THREADS || thread_id < 1) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
          }
          else {
            pthread_mutex_lock(&data->threads[thread_id-1].mutex_wait_thread);
            data->threads[thread_id-1].delay += delay;
            pthread_mutex_unlock(&data->threads[thread_id-1].mutex_wait_thread);
          }
        }
        
        break;

      case CMD_INVALID:
 
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
 
        pthread_mutex_unlock(&mutex);
        printf(
            "Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms> [thread_id]\n"  
            "  BARRIER\n"                      
            "  HELP\n");

        break;

      case CMD_BARRIER:

        data->barrier = 1;
        pthread_mutex_unlock(&mutex);  
        break;
        
      case CMD_EMPTY:
        pthread_mutex_unlock(&mutex);
        break;

      case EOC:
        pthread_mutex_unlock(&mutex);
        *return_value = 0;
        return (void*)return_value;
    }    
  }
}
