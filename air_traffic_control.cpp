#include "pthread_sleep.c"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <queue>

// Used namespace, since I didn't want to deal with using std::vector in front of the queue initializations.
using namespace std;

/* Plane struct is used throughout our implementation:
 * ID: unique identification number for a plane.
 * arrival_time: the time in our simulation when the plane thread is created.
 * runway_time: the time where the plane reaches at the end of the runway.
 * wait_at_front_time: the time where how much the plane waits when it is at the front of the queue (used for handling
 * starvation)
 * lock and cond: used for signaling and waiting of the plane threads.
 */
struct Plane {
    int ID;
    time_t arrival_time;
    time_t wait_at_front_time;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

// Three queues needed for main implementation of the system.
queue<Plane> landing_queue;
queue<Plane> departing_queue;
queue<Plane> emergency_queue;

// Three int queues needed when we start printing log after nth second until the end of the simulation to the terminal.
queue<int> landing_queue_of_ID;
queue<int> emergency_queue_of_ID;
queue<int> departing_queue_of_ID;

// 2D Array, where we handle planes.log
char planes_log[350][57];

/* The lock and cond, where we need at the start of the sim in order for the first plane ever to signal atc to start
 * working.
 */
pthread_mutex_t atc_lock;
pthread_cond_t atc_cond;

// The identification integers initialization for the planes. Landing ones are even, departing ones are odd.
int unique_departing_plane_id = 1;
int unique_landing_plane_id = 0;

// The file pointer initialization where we use it for planes.log
FILE *fp = fopen("./planes.log", "w");

// The variables that are needed for command line arguments.
int simulation_duration;
float p;
int n;
int seed;

/* The other variables I needed in order to complete this project:
 *  total_planes_in_sim: Used for printing planes.log
 *  current_time and start_time: Self-explanatory.
 *  t: The t variable stated in the PDF.
 */
int total_planes_in_sim = 0;
time_t current_time;
time_t start_time;
int t = 1;

/* landing_func is a function, which takes a bool as an argument, where it is one of the thread functions that a plane
 * thread can use. It basically initializes the plane struct, completes some of the planes log and waits until it is
 * signaled by the ATC, then exits the thread.
 */
void *landing_func(void *emergency_arg) {
    // Casting emergency_arg to a boolean, since threads work that way.
    bool emergency = (bool) emergency_arg;

    /* Constructing some of the elements of the struct Plane and incrementing total_planes_in_sim, since it is needed in
     * logging.
     */
    struct Plane plane;
    pthread_mutex_init(&plane.lock, NULL);
    pthread_cond_init(&plane.cond, NULL);
    total_planes_in_sim++;

    /* Initialization of ID of the plane and also starting to log ID in this part to correct position. ID can support
     * max of 999.
     */
    plane.ID = unique_landing_plane_id;

    char plane_id_string[3];
    sprintf(plane_id_string, "%d", unique_landing_plane_id);
    if (unique_landing_plane_id >= 100) {
        planes_log[unique_landing_plane_id][0] = plane_id_string[0];
        planes_log[unique_landing_plane_id][1] = plane_id_string[1];
        planes_log[unique_landing_plane_id][2] = plane_id_string[1];
    } else if (unique_landing_plane_id >= 10) {
        planes_log[unique_landing_plane_id][0] = plane_id_string[0];
        planes_log[unique_landing_plane_id][1] = plane_id_string[1];
    } else {
        planes_log[unique_landing_plane_id][0] = plane_id_string[0];
    }

    /* Initialization of arrival_time of the plane and also starting to log Request Time in this part to correct
     * position. ID can support a max of 999.
     */
    plane.arrival_time = current_time - start_time;

    char request_time_string[3];
    sprintf(request_time_string, "%ld", plane.arrival_time);
    if (plane.arrival_time >= 100) {
        planes_log[unique_landing_plane_id][20] = request_time_string[0];
        planes_log[unique_landing_plane_id][21] = request_time_string[1];
        planes_log[unique_landing_plane_id][22] = request_time_string[2];
    } else if (plane.arrival_time >= 10) {
        planes_log[unique_landing_plane_id][20] = request_time_string[0];
        planes_log[unique_landing_plane_id][21] = request_time_string[1];
    } else {
        planes_log[unique_landing_plane_id][20] = request_time_string[0];
    }

    /* If the plane is an emergency plane, push it to the emergency queue and do the log according to that, else push it
     * to the landing queue and do the log according to that. Also add them both to their id_queue, which is needed for
     * terminal log.
     */
    if (emergency) {
        emergency_queue.push(plane);
        emergency_queue_of_ID.push(plane.ID);
        planes_log[unique_landing_plane_id][8] = '(';
        planes_log[unique_landing_plane_id][9] = 'E';
        planes_log[unique_landing_plane_id][10] = ')';
        planes_log[unique_landing_plane_id][11] = 'L';
    } else {
        landing_queue.push(plane);
        planes_log[unique_landing_plane_id][9] = 'L';
        landing_queue_of_ID.push(plane.ID);
    }

    // If this is the first plane ever of the simulation signal the atc to start working.
    if (unique_landing_plane_id == 0) {
        pthread_cond_signal(&atc_cond);
    }

    // Increment unique_landing_plane_id by two for the next plane.
    unique_landing_plane_id = unique_landing_plane_id + 2;

    // Wait on the plane's own cond and lock until it is signalled by the ATC thread.
    pthread_cond_wait(&plane.cond, &plane.lock);

    // After it gets signalled, exit the thread, symbolizing landing.
    pthread_exit(NULL);
}

/* departing_func is a function, where it is one of the thread functions that a plane thread can use. It basically
 * initializes the plane struct, completes some of the planes log and waits until it is signaled by the ATC, then exits
 * the thread.
 */
void *departing_func(void *param) {
    /* Constructing some of the elements of the struct Plane and incrementing total_planes_in_sim, since it is needed in
     * logging.
     */
    struct Plane plane;
    plane.wait_at_front_time = 0;
    pthread_mutex_init(&plane.lock, NULL);
    pthread_cond_init(&plane.cond, NULL);
    total_planes_in_sim++;

    /* Initialization of ID of the plane and also starting to log ID in this part to correct position. ID can support
    * max of 999.
    */
    plane.ID = unique_departing_plane_id;

    char plane_id_string[3];
    sprintf(plane_id_string, "%d", unique_departing_plane_id);
    if (unique_departing_plane_id >= 100) {
        planes_log[unique_departing_plane_id][0] = plane_id_string[0];
        planes_log[unique_departing_plane_id][1] = plane_id_string[1];
        planes_log[unique_departing_plane_id][2] = plane_id_string[1];
    } else if (unique_departing_plane_id >= 10) {
        planes_log[unique_departing_plane_id][0] = plane_id_string[0];
        planes_log[unique_departing_plane_id][1] = plane_id_string[1];
    } else {
        planes_log[unique_departing_plane_id][0] = plane_id_string[0];
    }

    /* Initialization of arrival_time of the plane and also starting to log Request Time in this part to correct
     * position. ID can support a max of 999.
     */
    plane.arrival_time = current_time - start_time;

    char request_time_string[3];
    sprintf(request_time_string, "%ld", plane.arrival_time);
    if (plane.arrival_time >= 100) {
        planes_log[unique_departing_plane_id][20] = request_time_string[0];
        planes_log[unique_departing_plane_id][21] = request_time_string[1];
        planes_log[unique_departing_plane_id][22] = request_time_string[2];
    } else if (plane.arrival_time >= 10) {
        planes_log[unique_departing_plane_id][20] = request_time_string[0];
        planes_log[unique_departing_plane_id][21] = request_time_string[1];
    } else {
        planes_log[unique_departing_plane_id][20] = request_time_string[0];
    }

    // Specifying it is a departure plane to the planes.log
    planes_log[unique_departing_plane_id][9] = 'D';

    // After modifying everything we need, adding the plane to the planes queue and the ID queue.
    departing_queue_of_ID.push(plane.ID);
    departing_queue.push(plane);

    // If this is the first plane ever, signal the ATC to start working until the end of the simulation.
    if (unique_departing_plane_id == 1) {
        pthread_cond_signal(&atc_cond);
    }

    // Increment unique_landing_plane_id by two for the next plane.
    unique_departing_plane_id = unique_departing_plane_id + 2;

    // Wait on the plane's own cond and lock until it is signalled by the ATC thread.
    pthread_cond_wait(&plane.cond, &plane.lock);

    // After it gets signalled, exit the thread, symbolizing departing.
    pthread_exit(NULL);
}

/* air_traffic_control is a function, where it is one of the thread functions that a thread can use. Only a single
 * thread uses the air_traffic_control function. It handles landing, departing and if a plane is able to land/depart,
 * completes the rest of the logging.
 */
void *air_traffic_control(void *param) {
    // The thread waits for the first plane to signal this in order for simulation to start.
    pthread_cond_wait(&atc_cond, &atc_lock);

    // current_time might change, with waiting, therefore reassigning the value after the cond_wait.
    current_time = time(NULL);

    // After the first plane, the atc thread goes in a loop, where it runs until the end of the simulation.
    while (current_time < start_time + simulation_duration) {
        // We start with handling the emergency_queue, since it has priority and it needs to land as soon as possible.
        if (emergency_queue.size() > 0) {
            // We sleep the thread  for 2t seconds in order to simulate landing and get a new current_time.
            pthread_sleep(2 * t);
            current_time = time(NULL);

            // We signal the plane thread in order to make it stop cond_wait and exit the landing_func.
            pthread_cond_signal(&emergency_queue.front().cond);

            /* Then we calculate its Runway Time and Turnaround Time and add it to our planes.log which supports up to
             * 999
             */
            long int runway_time = current_time - start_time;

            char runway_time_string[3];
            sprintf(runway_time_string, "%ld", runway_time);
            if (runway_time >= 100) {
                planes_log[emergency_queue.front().ID][33] = runway_time_string[0];
                planes_log[emergency_queue.front().ID][34] = runway_time_string[1];
                planes_log[emergency_queue.front().ID][35] = runway_time_string[2];
            } else if (runway_time >= 10) {
                planes_log[emergency_queue.front().ID][33] = runway_time_string[0];
                planes_log[emergency_queue.front().ID][34] = runway_time_string[1];
            } else {
                planes_log[emergency_queue.front().ID][33] = runway_time_string[0];
            }

            long int turnaround_time = runway_time - emergency_queue.front().arrival_time;

            char turnaround_time_string[3];
            sprintf(turnaround_time_string, "%ld", turnaround_time);
            if (turnaround_time >= 100) {
                planes_log[emergency_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[emergency_queue.front().ID][48] = turnaround_time_string[1];
                planes_log[emergency_queue.front().ID][49] = turnaround_time_string[2];
            } else if (turnaround_time >= 10) {
                planes_log[emergency_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[emergency_queue.front().ID][48] = turnaround_time_string[1];
            } else {
                planes_log[emergency_queue.front().ID][47] = turnaround_time_string[0];
            }

            // After we are done with the plane, we also pop them out of both queues.
            emergency_queue.pop();
            emergency_queue_of_ID.pop();

        /* After the emergency queue, it is time to handle the departing queue, It is a complicated one since it handles
         * the starvation in both the departing queue and the landing queue. If landing queue size is smaller than 12
         * and departing queue is bigger than 0 and (departing queue's first plane's  waiting in front of the queue is
         * larger than 2 or landing queue is empty, it will land a plane from the departing queue. The stuff with % 40
         * was added, in order to make the emergency plane land faster which has nothing to do with starvation
         * implementation in part II. This is implementation makes the final difference between planes on air and ground
         * 1-4, every time I run it, so it still favors the landing planes, but no starvation in both queues for 60s
         * simulation. The rest of the stuff has the same explanation as the emergency_queue, it is pretty much the same
        * implementation as the emergency queue. The important part is explained above.
        */

        } else if (landing_queue.size() < 12 && departing_queue.size() > 0 &&
                   (departing_queue.front().wait_at_front_time > 2 * t || landing_queue.empty()) &&
                   (((current_time - start_time) % (40 * t)) != 0 || current_time - start_time == 0)) {
            pthread_sleep(2 * t);
            current_time = time(NULL);
            pthread_cond_signal(&departing_queue.front().cond);

            long int runway_time = current_time - start_time;

            char runway_time_string[3];
            sprintf(runway_time_string, "%ld", runway_time);
            if (runway_time >= 100) {
                planes_log[departing_queue.front().ID][33] = runway_time_string[0];
                planes_log[departing_queue.front().ID][34] = runway_time_string[1];
                planes_log[departing_queue.front().ID][35] = runway_time_string[2];
            } else if (runway_time >= 10) {
                planes_log[departing_queue.front().ID][33] = runway_time_string[0];
                planes_log[departing_queue.front().ID][34] = runway_time_string[1];
            } else {
                planes_log[departing_queue.front().ID][33] = runway_time_string[0];
            }

            long int turnaround_time = runway_time - departing_queue.front().arrival_time;

            char turnaround_time_string[3];
            sprintf(turnaround_time_string, "%ld", turnaround_time);
            if (turnaround_time >= 100) {
                planes_log[departing_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[departing_queue.front().ID][48] = turnaround_time_string[1];
                planes_log[departing_queue.front().ID][49] = turnaround_time_string[2];
            } else if (turnaround_time >= 10) {
                planes_log[departing_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[departing_queue.front().ID][48] = turnaround_time_string[1];
            } else {
                planes_log[departing_queue.front().ID][47] = turnaround_time_string[0];
            }

            departing_queue.pop();
            departing_queue_of_ID.pop();

        /* This part is for the landing queue, the only reason why it has conditions is that so emergency_plane gets
         * landed as soon as possible, other than that, inside implementation is pretty much similar to emergency
         * queue's, which was explained above.
         */
        } else if ((((current_time - start_time) % (40 * t)) != 0 || current_time - start_time == 0)) {
            pthread_sleep(2 * t);
            current_time = time(NULL);
            pthread_cond_signal(&landing_queue.front().cond);

            long int runway_time = current_time - start_time;

            char runway_time_string[3];
            sprintf(runway_time_string, "%ld", runway_time);
            if (runway_time >= 100) {
                planes_log[landing_queue.front().ID][33] = runway_time_string[0];
                planes_log[landing_queue.front().ID][34] = runway_time_string[1];
                planes_log[landing_queue.front().ID][35] = runway_time_string[2];
            } else if (runway_time >= 10) {
                planes_log[landing_queue.front().ID][33] = runway_time_string[0];
                planes_log[landing_queue.front().ID][34] = runway_time_string[1];
            } else {
                planes_log[landing_queue.front().ID][33] = runway_time_string[0];
            }

            long int turnaround_time = runway_time - landing_queue.front().arrival_time;
            char turnaround_time_string[3];
            sprintf(turnaround_time_string, "%ld", turnaround_time);
            if (turnaround_time >= 100) {
                planes_log[landing_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[landing_queue.front().ID][48] = turnaround_time_string[1];
                planes_log[landing_queue.front().ID][49] = turnaround_time_string[2];
            } else if (turnaround_time >= 10) {
                planes_log[landing_queue.front().ID][47] = turnaround_time_string[0];
                planes_log[landing_queue.front().ID][48] = turnaround_time_string[1];
            } else {
                planes_log[landing_queue.front().ID][47] = turnaround_time_string[0];
            }

            landing_queue.pop();
            landing_queue_of_ID.pop();
        }
        /* Incrementing departing_queue's front plane's wait at front time which was used for my starvation
         * implementation.
         */
        departing_queue.front().wait_at_front_time = departing_queue.front().wait_at_front_time + 2 * t;
    }
    // After the simulation is over, we exit the atc thread.
    pthread_exit(NULL);
}

/* print_debug function was used to print out the planes on air and on ground after nth second. It used the queues with
 * ids.
 */
void print_debug() {
    printf("At %ld sec ground: ", current_time - start_time);
    for (int i = 0; i < departing_queue_of_ID.size(); i++) {
        int id = departing_queue_of_ID.front();
        printf("%d ", id);
        departing_queue_of_ID.pop();
        departing_queue_of_ID.push(id);
    }

    printf("\nAt %ld sec air: ", current_time - start_time);
    for (int i = 0; i < landing_queue_of_ID.size(); i++) {
        int id = landing_queue_of_ID.front();
        printf("%d ", id);
        landing_queue_of_ID.pop();
        landing_queue_of_ID.push(id);
    }
    for (int i = 0; i < emergency_queue_of_ID.size(); i++) {
        int id = emergency_queue_of_ID.front();
        printf("%d ", id);
        emergency_queue_of_ID.pop();
        emergency_queue_of_ID.push(id);
    }

    printf("%s", "\n");
}

/* log_initialization is a function where we initialize the planes_log with empty characters and add end of string
 * character at the end of every line.
 */
void log_initialization() {
    for (int i = 0; i < 350; i++) {
        for (int j = 0; j < 56; j++) {
            planes_log[i][j] = ' ';
            planes_log[i][56] = '\0';
        }
    }
}

// print_log is a function that was used at the end of the main in order to print our final log to the planes.log file.
void print_log() {
    fprintf(fp, "%s", "PlaneID Status Request Time Runway Time Turnaround Time\n");
    fprintf(fp, "%s", "________________________________________________________\n");
    for (int i = 0; i < total_planes_in_sim; i++) {
        fprintf(fp, "%s\n", planes_log[i]);
    }
}

/* the main function handles parsing command line arguments to variables, initializes variables, mutexes, conds, log and
 * other stuff we need. It creates the atc, first landing and departing planes, after that it starts to create an
 * emergency plane every 40t seconds and creates a landing plane with probability p, and a departing plane with
 * probability 1-p every t seconds. It also handles printing out to terminal log after nth second, every second. Finally,
 * it handles printing out the log to planes.log and closes the file pointer.
 */
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            simulation_duration = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-p") == 0) {
            p = (double) atof(argv[i + 1]);
        } else if (strcmp(argv[i], "-n") == 0) {
            n = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-seed") == 0) {
            seed = atoi(argv[i + 1]);
            srand(seed);
        }
    }

    log_initialization();
    pthread_mutex_init(&atc_lock, NULL);
    pthread_cond_init(&atc_cond, NULL);
    pthread_t atc, departing_plane, landing_plane;
    pthread_create(&atc, NULL, air_traffic_control, NULL);
    pthread_create(&landing_plane, NULL, landing_func, (void *) 0);
    pthread_create(&departing_plane, NULL, departing_func, NULL);

    start_time = time(NULL);
    current_time = time(NULL);
    double random_number;
    while (current_time < start_time + simulation_duration) {
        random_number = (double) rand() / (RAND_MAX);
        if ((((current_time - start_time) % (40 * t)) == 0) && current_time - start_time != 0) {
            pthread_t new_plane;
            pthread_create(&new_plane, NULL, landing_func, (void *) 1);
        } else if (random_number <= p) {
            pthread_t new_plane;
            pthread_create(&new_plane, NULL, landing_func, (void *) 0);
        }
        if (random_number <= 1 - p) {
            pthread_t new_plane;
            pthread_create(&new_plane, NULL, departing_func, NULL);
        }
        if (current_time - start_time >= n) {
            print_debug();
            fflush(stdout);
        }
        pthread_sleep(t);
        current_time = time(NULL);
    }
    print_log();
    fclose(fp);
    return 0;
}