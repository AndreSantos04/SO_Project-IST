#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/io.h"
#include "eventlist.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @param from First node to be searched.
/// @param to Last node to be searched.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    lock_printf();
    fprintf(stderr, "EMS state has already been initialized\n");
    unlock_printf();
    return 1;
  }

  event_list = create_list();
  state_access_delay_us = delay_us;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    lock_printf();
    fprintf(stderr, "EMS state must be initialized\n");
    unlock_printf();
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking list rwl\n");
    unlock_printf();
    return 1;
  }
  
  free_list(event_list);
  pthread_rwlock_unlock(&event_list->rwl);
  free(event_list); 
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  if (event_list == NULL) {
    lock_printf();
    fprintf(stderr, "EMS state must be initialized\n");
    unlock_printf();
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking list rwl\n");
    unlock_printf();
    return 1;
  }


  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
      lock_printf();
      fprintf(stderr, "Event already exists\n");
      unlock_printf();
      pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    lock_printf();
    fprintf(stderr, "Error allocating memory for event\n");
    unlock_printf();
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }


  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    pthread_rwlock_unlock(&event_list->rwl);
    lock_printf();
    fprintf(stderr, "Error initializing mutex\n");
    unlock_printf();
    free(event);
    return 1;
  }


  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));

  if (event->data == NULL) {
    lock_printf();
    fprintf(stderr, "Error allocating memory for event data\n");
    unlock_printf();
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }



  if (append_to_list(event_list, event) != 0) {
    lock_printf();
    fprintf(stderr, "Error appending event to list\n");
    unlock_printf();
    pthread_rwlock_unlock(&event_list->rwl);
    free(event->data);
    free(event);
    return 1;
  }
;
  event_list->num_events++;
  pthread_rwlock_unlock(&event_list->rwl);

  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    lock_printf();
    fprintf(stderr, "EMS state must be initialized\n");
    unlock_printf();
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking list rwl\n");
    unlock_printf();
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    lock_printf();
    fprintf(stderr, "Event not found\n");
    unlock_printf();
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking mutex\n");
    unlock_printf();
    return 1;
  }

  for (size_t i = 0; i < num_seats; i++) {
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      lock_printf();
      fprintf(stderr, "Seat out of bounds\n");
      unlock_printf();
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        lock_printf();
        fprintf(stderr, "Seat already reserved\n");
        unlock_printf();
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }


  pthread_mutex_unlock(&event->mutex);
  return 0;
}

void* ems_show(unsigned int event_id) {
  int* error_return = malloc(sizeof(int));
  if(error_return == NULL){
    lock_printf();
    fprintf(stderr, "Error allocating memory for error_return\n");
    unlock_printf();
    return NULL;
  }
  *error_return = 1;

  if (event_list == NULL) {
    lock_printf();
    fprintf(stderr, "EMS state must be initialized\n");
    unlock_printf();
    return (void*)error_return;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking list rwl\n");
    unlock_printf();
    return (void*)error_return;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    lock_printf();
    fprintf(stderr, "Event not found\n");
    unlock_printf();
    return (void*)error_return;
  }
  size_t buf_size = sizeof(int)+2*sizeof(size_t)+sizeof(unsigned int)*event->rows*event->cols;
  int default_return = 0;
  
  void* buf = malloc(buf_size);
  if (buf == NULL) {
    lock_printf();
    fprintf(stderr, "Error reallocating memory for buffer\n");
    unlock_printf();
    return (void*)error_return;
  }
  
  store_data(buf,&default_return,sizeof(int));
  store_data(buf + sizeof(int), &event->rows, sizeof(size_t));
  store_data(buf + sizeof(int) + sizeof(size_t), &event->cols, sizeof(size_t));
  store_data(buf + sizeof(int) + 2*sizeof(size_t), event->data, sizeof(unsigned int)*event->rows*event->cols);
  
  
  free(error_return);
  return buf;
}

void* ems_list_events(){
  int* error_return = malloc(sizeof(int));
  if(error_return == NULL){
    lock_printf();
    fprintf(stderr, "Error allocating memory for error_return\n");
    unlock_printf();
    return NULL;
  }
  *error_return = 1;

  if (event_list == NULL) {
    lock_printf();
    fprintf(stderr, "EMS state must be initialized\n");
    unlock_printf();
    return (void*)error_return;
  }
  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    lock_printf();
    fprintf(stderr, "Error locking list rwl\n");
    unlock_printf();
    return (void*)error_return;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;
  unsigned int* ids = malloc(sizeof(unsigned int) * event_list->num_events);
  if (ids == NULL) {
    lock_printf();
    fprintf(stderr, "Error allocating memory for ids\n");
    unlock_printf();
    return (void*)error_return;
  }

  size_t buf_size = sizeof(int)+sizeof(size_t)+sizeof(unsigned int) * event_list->num_events;
  void* buf = malloc(buf_size);
  if (buf == NULL) {
    lock_printf();
    fprintf(stderr, "Error reallocating memory for buffer\n");
    unlock_printf();
    free(ids);
    return (void*)error_return;
  }
  int default_return = 0;

  store_data(buf,&default_return,sizeof(int));
  store_data(buf + sizeof(int), &event_list->num_events, sizeof(size_t));
  
  if(event_list->num_events == 0){
    free(error_return);
    free(ids);
    return buf;
  }
  int i = 0;

  while (1) {
    ids[i] = current->event->id;
    if (current == to) {
      break;
    }
    current = current->next;
    i++;
  }
  store_data(buf + sizeof(int) + sizeof(size_t), ids, sizeof(unsigned int) * event_list->num_events);

  free(error_return);
  free(ids);
  pthread_rwlock_unlock(&event_list->rwl);
  return buf;
}

int ems_show_all(){
  

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }
  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* to = event_list->tail;
  struct ListNode* current = event_list->head;
  struct ListNode* temp = event_list->head;
  while(temp!= NULL){
    pthread_mutex_lock(&temp->event->mutex);
    temp = temp->next;  
  }
  // Usamos o lock_printf no decorrer da função de modo a que o print para 
  // o output no caso do SIGUSR1 seja atómico
  lock_printf();
  size_t it = 0;
  while (it < event_list->num_events) {
    char buf[16];
    sprintf(buf, "Event %u\n", current->event->id);
    
    if(print_str(STDOUT_FILENO, buf)){
      perror("Error writing to file descriptor");
      unlock_printf();
      pthread_rwlock_unlock(&event_list->rwl);    
      break;
    }

    int stop = 0;
    unsigned int* event_data = current->event->data;
    for (size_t i = 1; i <= current->event->rows; i++) {
      for (size_t j = 1; j <= current->event->cols; j++) {
        char buffer[16];
        sprintf(buffer, "%u", event_data[seat_index(current->event, i, j)]);

        if (print_str(STDOUT_FILENO, buffer)) {
          perror("Error writing to file descriptor");          
          stop = 1;
          break;
        }

        if (j < current->event->cols) {
          if (print_str(STDOUT_FILENO, " ")) {
            perror("Error writing to file descriptor");  
            stop = 1;
            break;
          }
        }
        if(stop){
          break;
        }
      }
      if(stop){
        break;
      }
      if (print_str(STDOUT_FILENO, "\n")) {
        perror("Error writing to file descriptor");
        break;
      }
    }
    if(stop){
      break;
    }
    if (current == to) {
      break;
    }
    current = current->next;
    it++;
  }
  unlock_printf();
  temp = event_list->head;
  while(temp != NULL){
    pthread_mutex_unlock(&temp->event->mutex);
    temp = temp->next;  
  }
  pthread_rwlock_unlock(&event_list->rwl); 
  
  return 0;
}

