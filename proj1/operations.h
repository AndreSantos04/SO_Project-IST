#ifndef EMS_OPERATIONS_H
#define EMS_OPERATIONS_H

#include <stddef.h>
#include <pthread.h>
#include <unistd.h>
#include "constants.h"
#include "parser.h"

typedef struct{
    pthread_t thread;
    unsigned int delay;
    int waited;
    pthread_mutex_t mutex_wait_thread;
} thread_info;

typedef struct{
    thread_info* threads;
    int barrier;  
    int fdW;
    int fdR;
} threads_data;

/*void set_barrier(int i);
void free_barrier();*/

/*void set_wait_thread_id(unsigned int thread_id, unsigned int delay);
void set_all_threads_wait(unsigned int delay);
void should_thread_wait(unsigned int thread_id);*/


/// Initializes the EMS state.
/// @param delay_ms State access delay in milliseconds.
/// @return 0 if the EMS state was initialized successfully, 1 otherwise.
int ems_init(unsigned int delay_ms);

/// Destroys the EMS state.
int ems_terminate();

/// Creates a new event with the given id and dimensions.
/// @param event_id Id of the event to be created.
/// @param num_rows Number of rows of the event to be created.
/// @param num_cols Number of columns of the event to be created.
/// @return 0 if the event was created successfully, 1 otherwise.
int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols);

/// Creates a new reservation for the given event.
/// @param event_id Id of the event to create a reservation for.
/// @param num_seats Number of seats to reserve.
/// @param xs Array of rows of the seats to reserve.
/// @param ys Array of columns of the seats to reserve.
/// @return 0 if the reservation was created successfully, 1 otherwise.
int ems_reserve(unsigned int event_id, size_t num_seats, Coordinate* coords);

/// Prints the given event.
/// @param event_id Id of the event to print.
/// @return 0 if the event was printed successfully, 1 otherwise.
int ems_show(unsigned int event_id, int fd);

/// Prints all the events.
/// @return 0 if the events were printed successfully, 1 otherwise.
int ems_list_events(int fd);

/// Waits for a given amount of time.
/// @param delay_us Delay in milliseconds.
void ems_wait(unsigned int delay_ms); 


#endif  // EMS_OPERATIONS_H
