#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include "eventlist.h"
#include "constants.h"
#include "operations.h"
#include "parser.h"

pthread_mutex_t OutFileWritemutex = PTHREAD_MUTEX_INITIALIZER;

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  event_list->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  event_list->id_being_processed = -1;
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    
    return 1;
  }
  pthread_mutex_lock(&event_list->mutex);
  if (get_event_with_delay(event_id) != NULL || event_list->id_being_processed == event_id) {
    fprintf(stderr, "Event already exists\n");
    pthread_mutex_unlock(&event_list->mutex);
    return 1;
  }
  event_list->id_being_processed = event_id;
  pthread_mutex_unlock(&event_list->mutex);

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  event->rw_lock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
  
  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    
    return 1;
  }
  
  for (size_t i = 0; i < num_rows * num_cols; i++) {
    event->data[i] = 0;
  }
  pthread_mutex_lock(&event_list->mutex);
  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    event_list->id_being_processed = -1;
    pthread_mutex_unlock(&event_list->mutex);
    return 1;
  }
  event_list->id_being_processed = -1;
  pthread_mutex_unlock(&event_list->mutex);

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, Coordinate* coords) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }
  
  pthread_mutex_lock(&event_list->mutex);
  struct Event* event = get_event_with_delay(event_id);
  pthread_mutex_unlock(&event_list->mutex);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  pthread_rwlock_wrlock(&event->rw_lock);
  unsigned int reservation_id = ++event->reservations;
  pthread_rwlock_unlock(&event->rw_lock);
  size_t i = 0;
  for (; i < num_seats; i++) {
    size_t row = coords[i].x;
    size_t col = coords[i].y;
    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      fprintf(stderr, "Invalid seat\n");
      break;
    }

    pthread_rwlock_wrlock(&event->rw_lock);
  
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      fprintf(stderr, "Seat already reserved\n");
      pthread_rwlock_unlock(&event->rw_lock);
      break;
    }
    *get_seat_with_delay(event, seat_index(event, row, col)) = reservation_id;
    pthread_rwlock_unlock(&event->rw_lock);  
  }

  if (i < num_seats) {
    pthread_rwlock_wrlock(&event->rw_lock);
    event->reservations--; 
    for (size_t j = 0; j < i; j++) {
      *get_seat_with_delay(event, seat_index(event, coords[j].x, coords[j].y)) = 0;
    }
    pthread_rwlock_unlock(&event->rw_lock);
  }
  return 0;
}

int ems_show(unsigned int event_id, int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    
    return 1;
  }
  pthread_mutex_lock(&event_list->mutex);
  struct Event* event = get_event_with_delay(event_id);
  pthread_mutex_unlock(&event_list->mutex);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  size_t buf_size = (unsigned long)sizeof(char)*event->cols*event->rows*2;
  char* buffer = malloc(buf_size);

  if(buffer == NULL){
    fprintf(stderr, "Error allocating memory for buffer\n");
    return 1;
  }
  
  size_t buf_iX = 0;

  pthread_rwlock_wrlock(&event->rw_lock);
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));
      if (seat==NULL) {
        fprintf(stderr, "Error getting seat\n");
        pthread_rwlock_unlock(&event->rw_lock);
        free(buffer);
        return 1;
      }
      char stringToAdd[CMD_BUF_SIZE] = "";
      char* seatStr = intToString(*seat);
      strcat(stringToAdd, seatStr);
      free(seatStr);
      if(buf_iX >= buf_size){
        buf_size *= 2;
        char* temp = realloc(buffer, buf_size);
        if(temp == NULL){
          fprintf(stderr, "Error allocating memory for buffer\n");
          free(buffer);
          pthread_rwlock_unlock(&event->rw_lock);
          return 1;
        }
        buffer = temp;
      }
      size_t k = 0;
      while(k < strlen(stringToAdd)){
        buffer[buf_iX] = stringToAdd[k];
        buf_iX++;
        k++;
      }
      
      if (j < event->cols) {
        buffer[buf_iX] = ' ';
        buf_iX++;
      } 
    }
    buffer[buf_iX] = '\n';
    buf_iX++;
  }
  pthread_rwlock_unlock(&event->rw_lock);

  pthread_mutex_lock(&OutFileWritemutex);
  if(buf_iX > 0){
    if(safe_write(fd, buffer, buf_iX) == -1){
      fprintf(stderr, "Error writing to file\n");
      pthread_mutex_unlock(&OutFileWritemutex);
      free(buffer);
      return 1;
    }
  }
  pthread_mutex_unlock(&OutFileWritemutex);

  free(buffer); 
  return 0;
}



int ems_list_events(int fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  pthread_mutex_lock(&event_list->mutex);
  struct ListNode* current = event_list->head;
  if (current == NULL) {
    char buffer[11]= "No events\n";
    pthread_mutex_lock(&OutFileWritemutex);
    if(safe_write(fd, buffer, strlen(buffer)) == -1){
      fprintf(stderr, "Error writing to file\n");
      pthread_mutex_unlock(&OutFileWritemutex);
      pthread_mutex_unlock(&event_list->mutex);
      return 1;
    } 

    pthread_mutex_unlock(&OutFileWritemutex);
    pthread_mutex_unlock(&event_list->mutex);
    
    return 0;
  }
  
  
  pthread_mutex_unlock(&event_list->mutex);
  unsigned int buf_size = BUF_SIZE;
  char *buffer = malloc(sizeof(char)*buf_size);
 
  if(buffer == NULL){
    fprintf(stderr, "Error allocating memory for buffer\n");
    
    return 1;
  }
  
  size_t buf_iX = 0;
  while (current != NULL) {
    char stringToAdd[CMD_BUF_SIZE] = "";

    pthread_mutex_lock(&event_list->mutex);
    char* eventIdStr = intToString((current->event)->id);    
    pthread_mutex_unlock(&event_list->mutex);

    strcpy(stringToAdd, "Event: ");
    strcat(stringToAdd, eventIdStr);

    free(eventIdStr);

    if(buf_iX >= buf_size-strlen(stringToAdd)-1){
      buf_size *= 2;
      char* temp = realloc(buffer, buf_size);
      if(temp == NULL){
        fprintf(stderr, "Error allocating memory for buffer\n");
        free(temp);
        return 1;
      }
      buffer = temp;
    }

    size_t i = 0;
    while(i < strlen(stringToAdd)){
      buffer[buf_iX] = stringToAdd[i];
      buf_iX++;
      i++;
    }

    buffer[buf_iX] = '\n';
    buf_iX++;
    pthread_mutex_lock(&event_list->mutex);
    current = current->next;
    pthread_mutex_unlock(&event_list->mutex);
  }

  pthread_mutex_lock(&OutFileWritemutex);
  if(buf_iX > 0){
    if(safe_write(fd, buffer, buf_iX) == -1){
      fprintf(stderr, "Error writing to file\n");
      free(buffer);
      pthread_mutex_unlock(&OutFileWritemutex);
      return 1;
      }
  }
  free(buffer);
  pthread_mutex_unlock(&OutFileWritemutex);
  
  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

