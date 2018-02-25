#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

int * insert_quads (int step_1, int step_2, int step_3, int step_4);

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {

    int i = 0;

    //init the lanes and quad locks
    while (i < 4){

        pthread_mutex_init(&(isection.quad[i]), NULL);
        pthread_mutex_init(&(isection.lanes[i].lock), NULL);
        pthread_cond_init(&(isection.lanes[i].producer_cv), NULL);
        pthread_cond_init(&(isection.lanes[i].consumer_cv), NULL);

        isection.lanes[i].in_cars = NULL;
        isection.lanes[i].out_cars = NULL;
        isection.lanes[i].inc = 0;
        isection.lanes[i].passed = 0;
        isection.lanes[i].head = 0;
        isection.lanes[i].tail = 0;
        isection.lanes[i].capacity = LANE_LENGTH;
        isection.lanes[i].in_buf = 0;
        isection.lanes[i].buffer = malloc (sizeof(struct car*)*LANE_LENGTH);
        i++;
    }
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;//pointer to lane structure 
    int next_tail = 0;
    int prev_tail = 0;

    //check if in_cars has any cars left
    if (l->inc == 0){
        return NULL;
    }

    pthread_mutex_lock(&(l->lock));
    while (l->inc > 0){
    //block if the lane is full
        while (l->in_buf == l->capacity){
            pthread_cond_wait (&(l->producer_cv), &(l->lock));
        }

        //loop until you reach capacity or in_cars is empty
        while (l->in_buf < l->capacity && l->inc > 0){
            next_tail = (l->tail + 1) % (l->capacity);

            if (l->tail == 0){
                prev_tail = 9;
            }else{
                prev_tail = l->tail - 1;
            }

            //incoming car into buffer
            struct car *incoming_car = l->in_cars;
            
            //move in_cars ahead 
            l->in_cars = (l->in_cars)->next;

            //add to buffer
            l->buffer[l->tail] = incoming_car;
            incoming_car->next = l->buffer[prev_tail];

            l->tail = next_tail;

            // change counts
            l->inc = l->inc - 1;
            l->in_buf = l->in_buf + 1;
        }

        pthread_cond_signal(&(l->consumer_cv));
    }
    pthread_mutex_unlock(&(l->lock));
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
    struct lane *l = arg;
    int *path = NULL;
    int lock_index = 0;

    //nothing to do: terminate
    if (l->in_buf == 0 && l->inc ==0){
        return NULL;
    }

    pthread_mutex_lock(&(l->lock));

    // if there are cars waiting in in_cars keep looping
    while (l->inc > 0){
        
        while (l->in_buf == 0){
            pthread_cond_wait(&(l->consumer_cv), &(l->lock));
        }

        while (l->in_buf > 0){

            //compute path for car
            enum direction inc_dir = (l->buffer[l->head])->in_dir;
            enum direction outg_dir = (l->buffer[l->head])->out_dir;
            path = compute_path(inc_dir, outg_dir);

            //continue with quadrant lock checks
            lock_index = 0;
            while(lock_index < 4){
                if (path[lock_index] != -1){
                    pthread_mutex_lock(&(isection.quad[path[lock_index]]));
                }
                 lock_index = lock_index + 1;
            }



            //once quadrant locks are acquired, proceed to move into intersection
            struct car *crossing_car = l->buffer[l->head];
            struct lane *out_lane = &isection.lanes[outg_dir];

            //add car to out lane
            crossing_car->next = out_lane->out_cars;
            out_lane->out_cars = crossing_car;

            //inc head
            l->head = (l->head + 1) % (l->capacity);
            l->in_buf = l->in_buf - 1;

            printf("%d %d %d\n", crossing_car->in_dir, crossing_car->out_dir, crossing_car->id);


            //unlock quadrant locks
            lock_index = 0; 
            while(lock_index < 4){
                if (path[lock_index] != -1){
                    pthread_mutex_unlock(&(isection.quad[path[lock_index]]));
                }
                lock_index = lock_index + 1;
            }
            // free the computed path
            free(path);    
        }

        pthread_cond_signal(&(l->producer_cv));
    }

    free(l->buffer);
    pthread_mutex_unlock(&(l->lock));

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    //pointer to list on heap
    int *path = NULL;
    if (in_dir == out_dir){
        path = insert_quads(1,2,3,4);
    }else{
        switch(in_dir){
            case NORTH:
                if (out_dir == WEST)//Q2
                    path = insert_quads(2,0,0,0);
                else if (out_dir == SOUTH)//Q2 -> Q3
                    path = insert_quads(2,3,0,0);
                else if (out_dir == EAST)//Q2 -> Q3 -> Q4
                    path = insert_quads(2,3,4,0);
            break;

            case WEST:
                if (out_dir == SOUTH)//Q3
                    path = insert_quads(3,0,0,0);
                else if (out_dir == EAST)//Q3 -> Q4
                    path = insert_quads(3,4,0,0);
                else if (out_dir == NORTH)//Q1 -> Q3 -> Q4
                    path = insert_quads(1,3,4,0);
            break;

            case SOUTH:
                if (out_dir == EAST)//Q4
                    path = insert_quads(4,0,0,0);
                else if (out_dir == NORTH)//Q1 -> Q4
                    path = insert_quads(1,4,0,0);
                else if (out_dir == WEST)//Q1 -> Q2 -> Q4
                    path = insert_quads(1,2,4,0);
            break;

            case EAST:
                if (out_dir == NORTH)//Q1
                    path = insert_quads(1,0,0,0);
                else if (out_dir == WEST)//Q1 -> Q2
                    path = insert_quads(1,2,0,0);
                else if (out_dir == SOUTH)//Q1 -> Q2 -> Q3
                    path = insert_quads(1,2,3,0);
            break;

            default:
            break;     
        }
    }
    return path;
}

//function helper to insert quadrant path into heap
int* insert_quads(int step_1, int step_2, int step_3, int step_4){

    //insert into heap so the information isn't gone from the stack
    int *path = malloc(sizeof(int)*4);
    path[0] = step_1 - 1;
    path[1] = step_2 - 1;
    path[2] = step_3 - 1;
    path[3] = step_4 - 1;
    return path;
}
