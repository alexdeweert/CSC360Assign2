#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/queue.h>
#include <math.h>
#define BILLION 1000000000L
#define MILLION 1000000L
#define HUND_THOUSAND 100000L
#define NUM_THREADS 5
#define COUNT_LIM 13
#define DEBUG 0
#define LIVE 1
#define DEAD 0
#define EMPTY 0
#define FULL 1
#define TRUE 1
#define FALSE 0
#define QUEUE_EMPTY -1
//Structs
typedef struct Trains{
	char direction;
	char dir[4];
	long loading_time;
	long crossing_time;
	long train_id;
	long live;
	long ready;
	long dispatched;
	pthread_cond_t convar_cross;
} Train;

typedef struct Node {
	struct Node* next;
	int train_index;
} Node;

struct timespec start={0,0}, stop={0,0}, current={0,0};

//Queue heads
Node* eb_q = NULL;
Node* Eb_q = NULL;
Node* wb_q = NULL;
Node* Wb_q = NULL;

//Prototypes
void printTimeStamp();
void* dispatcher(void*);
void* train_function(void*);
void initializeThreads(Train**,pthread_t**);
void printArray(Train**);
void readInput(char*,Train**,int*);
void* load_controller(void*);
int resolveQueue(char,Train**);

//Mutexes
pthread_mutex_t mutex_load = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_dispatch = PTHREAD_MUTEX_INITIALIZER; //Train will lock this if ready to get sent to PQ
pthread_mutex_t mutex_cross = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_train_data = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;

//Convars
pthread_cond_t convar_begin_loading = PTHREAD_COND_INITIALIZER;
pthread_cond_t convar_dispatch = PTHREAD_COND_INITIALIZER;
pthread_cond_t convar_input_finished = PTHREAD_COND_INITIALIZER;

//Attributes
pthread_attr_t thread_attr; //also joinable
pthread_attr_t thread_attr_joinable;

//Globals and definitions
int num_live_threads = 0;
int num_loaded_trains = 0;
int train_count = 0;
int num_dispatched_trains = 0;
int sleptCount = 0;
int queue_count = 0;
char last_to_cross = '\0';
int accum;
int hours;
int minutes;
double seconds;


int main( int argc, char** argv ) {

	/* Initialize mutex and condition variable objects */
	pthread_attr_init(&thread_attr);
	pthread_attr_init(&thread_attr_joinable);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setdetachstate(&thread_attr_joinable, PTHREAD_CREATE_JOINABLE);
	
	int arr_size = 50;
	int i;
	Train* train_data;
	pthread_t* train_threads;
	pthread_t dispatcher_thread;
	long t;
	int rc;
	void* status;
	
	if( argc < 2 ) {
		printf("[ERROR] Must have at least one argument (HINT: The name of a text file with train data!)\n");
		return 1;
	}
	//Read input file and allocate train structs to array
	readInput(argv[1],&train_data,&arr_size);

	//Create all train threads
	initializeThreads(&train_data,&train_threads);	

	//Create dispatcher to poll loaded trains
	pthread_create( &dispatcher_thread, &thread_attr, dispatcher, (void*)&train_data );
	
	//Useful: https://computing.llnl.gov/tutorials/pthreads/samples/dotprod_mutex.c
	//Can get concurrency without having to call DETACH
	//by doing the join down here...
	pthread_attr_destroy(&thread_attr);
	pthread_attr_destroy(&thread_attr_joinable);
	pthread_join( dispatcher_thread, NULL );
	for( i = 0; i < train_count; i++ ) {
		pthread_join( train_threads[i], NULL );
	}

	//CLEAN UP
	pthread_mutex_destroy(&mutex_load);
	pthread_mutex_destroy(&mutex_dispatch);
	pthread_mutex_destroy(&mutex_cross);
	pthread_mutex_destroy(&mutex_train_data);
	pthread_mutex_destroy(&mutex_queue);
	//Convars
	pthread_cond_destroy(&convar_begin_loading);
	pthread_cond_destroy(&convar_dispatch);
	pthread_cond_destroy(&convar_input_finished);
	free(&train_data[0]);
	free(&train_threads[0]);
	train_data = NULL;
	train_threads = NULL;
	pthread_exit(NULL);
}

/*Can be used to very accurate timestamps if required*/
void printTimeStamp() {
	clock_gettime( CLOCK_MONOTONIC, &current);
	hours = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec )) / (double)3600.00), (double)24.00);
	minutes = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec )) / (double)60.00), (double)60.00);
	seconds = fmodf(((((double)current.tv_sec + 1.0e-9*current.tv_nsec ) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec ))), (double)60.00);
	//printf("\n%02d:%02d:%04.1f\n",hours,minutes,seconds);
	printf("%02d:%02d:%04.1f",hours,minutes,seconds);
}

/*Read in the train file*/
void readInput(char* filename, Train** train_data, int* arr_size) {
	char priority;
	int loading_time;
	int crossing_time;
	int i;
	(*train_data) = malloc(sizeof(Train)*(*arr_size)); //array of trains
	if( DEBUG ) printf("[DEBUG] Called readInput(), filename input was: \"%s\"\n", filename);
	FILE* in = fopen(filename, "r");
	while( fscanf( in, "%c %d %d *[\n]", &priority, &loading_time, &crossing_time ) != EOF ) {
		(*train_data)[train_count].direction = priority;
		(*train_data)[train_count].loading_time = loading_time;
		(*train_data)[train_count].crossing_time = crossing_time;
		(*train_data)[train_count].train_id = train_count;
		(*train_data)[train_count].live = LIVE;
		(*train_data)[train_count].ready = FALSE;
		(*train_data)[train_count].dispatched = FALSE;
		pthread_cond_init(&((*train_data)[train_count].convar_cross), NULL);
		switch(priority){
			case 'E': snprintf ( (*train_data)[train_count].dir, 5, "East"); break;
			case 'e': snprintf ( (*train_data)[train_count].dir, 5, "East"); break;
			case 'W': snprintf ( (*train_data)[train_count].dir, 5, "West"); break;
			case 'w': snprintf ( (*train_data)[train_count].dir, 5, "West"); break;
		}
		train_count++;
	}
}

void initializeThreads( Train** train_data, pthread_t** train_threads ) {

	(*train_threads) = malloc( sizeof(pthread_t)*train_count );
	int i;
	for( i = 0; i < train_count; i++ ) {
		pthread_mutex_lock(&mutex_load);
		if( DEBUG ) printf("Train %ld waiting to load, %d livethreads exist\n", (*train_data)[i].train_id, num_live_threads);
		num_live_threads++;
		pthread_create( &(*train_threads)[i], &thread_attr, train_function, (void*)&(*train_data)[i] );
		
		if( num_live_threads == train_count ) {
			pthread_cond_broadcast(&convar_begin_loading);
			clock_gettime(CLOCK_MONOTONIC, &start);
			pthread_cond_signal(&convar_dispatch);
		}
		pthread_mutex_unlock(&mutex_load);
	}
}

void* train_function( void* arg ) {

	Train *train_data = (Train*)arg;
	pthread_mutex_lock(&mutex_load);
		while( num_live_threads < train_count ) {
			pthread_cond_wait( &convar_begin_loading, &mutex_load );
			
		}
		if( DEBUG ) printf("Train %ld loading...\n", train_data->train_id);
	pthread_mutex_unlock(&mutex_load);
	usleep( train_data->loading_time * 100000 );
	if( DEBUG ) printf("Train %ld finished loading!\n", train_data->train_id);

	//Lock the queue mutex and add a new train entry to the appropriate queue
	pthread_mutex_lock(&mutex_queue);
	//Make new node
	//Node* temp = (Node*)malloc( sizeof(Node*) );
	Node* temp = malloc( sizeof(struct Node) );
	temp->train_index = train_data->train_id;
	temp->next = NULL;

	if( DEBUG ) printf("Adding train to queue: %d\n", temp->train_index);
	printTimeStamp();
	printf(" Train %ld is ready to go %s\n", train_data->train_id, train_data->dir);
	//Any loaded train goes into the loaded queue
	//Clearly, loaded trains get sent to their respective queues first
	if( train_data->direction == 'E') {
		if( Eb_q != NULL ) {
			Node* cur = Eb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			Eb_q = temp;
		}
	}
	else if( train_data->direction == 'W') {
		if( Wb_q != NULL ) {
			Node* cur = Wb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			Wb_q = temp;
		}
	}
	else if( train_data->direction == 'e') {
		if( eb_q != NULL ) {
			Node* cur = eb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			eb_q = temp;
		}

	}
	else { //its w
		if( wb_q != NULL ) {
			Node* cur = wb_q;
			while( cur->next != NULL ) {
				cur = cur->next;
			}
			cur->next = temp;
		}
		else {
			wb_q = temp;
		}
	}
	//to prevent spurious wakeup of dispatch convar
	queue_count++;
	//There are trains in the queues, signal dispatcher
	pthread_cond_signal(&convar_dispatch);
	pthread_mutex_unlock(&mutex_queue);

	//WAIT HERE FOR DISPATCHER TO SIGNAL GREEN LIGHT
	pthread_mutex_lock(&mutex_cross);
	while( !train_data->dispatched ) {
		pthread_cond_wait( &(train_data->convar_cross), &mutex_cross );
	}
	//Critical section here
	printTimeStamp();
	printf(" Train %ld is ON the main track going %s\n", train_data->train_id, train_data->dir);
	usleep( train_data->crossing_time * 100000 );
	printTimeStamp();
	printf(" Train %ld is OFF the main track after going %s\n", train_data->train_id, train_data->dir);
	num_dispatched_trains++;
	pthread_mutex_unlock(&mutex_cross);
	free(temp);
	pthread_exit((void*) 0);
}

//Poll loaded trains for dispatch
void* dispatcher( void* arg ) {

	//Wait until we get the greenlight from trains
	pthread_mutex_lock(&mutex_dispatch);
	while( queue_count < 1 ) {
		pthread_cond_wait(&convar_dispatch,&mutex_dispatch);	
	}
	pthread_mutex_unlock(&mutex_dispatch);

	Train** train_data = (Train**)arg;

	while(num_dispatched_trains < train_count) {
		int next_train;

		pthread_mutex_lock(&mutex_queue);
		//If EB high pri train, and other high-pri queue is empty, always send high pri over low pri
		
		//Two equal, high priority trains face eachother
		if( Eb_q != NULL && Wb_q != NULL ) {
			//Send E if west went last, or if none have gone
			if( last_to_cross == 'W' || last_to_cross == '\0' ) {
				next_train = resolveQueue('E',train_data);
				if( DEBUG ) printf("Popped from Eb_q: %d\n", next_train);
				Eb_q = Eb_q->next;
				last_to_cross = 'E';	
			}
			//Send W if east went last
			else if( last_to_cross == 'E' ) {
				next_train = Wb_q->train_index;
				if( DEBUG ) printf("Popped from Wb_q: %d\n", next_train);
				Wb_q = Wb_q->next;
				last_to_cross = 'W';
			}
		}

		//Eb ready, Wb null
		else if( Eb_q != NULL && Wb_q == NULL ) {
			//next_train = Eb_q->train_index;
			next_train = resolveQueue('E',train_data);
			if( DEBUG ) printf("Popped from Eb_q: %d\n", next_train);
			Eb_q = Eb_q->next;
			last_to_cross = 'E';
		}
		//Wb ready, Eb null
		else if( Wb_q != NULL && Eb_q == NULL ) {
			next_train = Wb_q->train_index;
			if( DEBUG ) printf("Popped from Wb_q: %d\n", next_train);
			Wb_q = Wb_q->next;
			last_to_cross = 'W';
		}
		pthread_mutex_unlock(&mutex_queue);

		pthread_mutex_lock(&mutex_cross);
		(*train_data)[next_train].dispatched = TRUE;
		pthread_cond_signal( &(*train_data)[next_train].convar_cross );
		pthread_mutex_unlock(&mutex_cross);	
	}
	pthread_exit((void*) 0);
}

/*Every time a train is selected to depart from a queue, FIRST
walk through the queue from tail to head to ensure that there isn't a
train that's ready with the same loading time, but a lower id (which indicates
that the train appeared in the input file first)
*/
int resolveQueue(char station, Train** train_data){
	long head_loading_time;
	int head_train_index;
	int do_swap = FALSE;
	Node* cur;
	switch(station){
		printf("Doing this\n");
		case 'E':
		if( Eb_q->next != NULL ) {
			head_loading_time = (*train_data)[Eb_q->train_index].loading_time;
			head_train_index = Eb_q->train_index;
			cur = Eb_q->next;
			while( cur != NULL ) {
				//compare current train heads loading time to each node, going backwards
				//If the current node loading time equal, but train_id is smaller...
				if( head_loading_time == (*train_data)[cur->train_index].loading_time && head_train_index > cur->train_index ) {
					//set head_train_index to the current node (in queue trace)
					head_train_index = cur->train_index;
					do_swap = TRUE;
				}
				cur = cur->next;
			}
			//Once the end of the queue is reached, the swap the node with highest_pri id and the train_id (that we started with)
			if( do_swap ) {
			    // Initialize previous and current pointers
			    Node* prev = Eb_q;
			    cur = Eb_q->next;
			 
			    Eb_q = cur;  // Change head before proceeeding
			 
			    // Traverse the list
			    while (1)
			    {
			        Node* next = cur->next;
			        cur->next = prev; // Change next of current as previous node
			 
			        // If next NULL or next is the last node
			        if (next == NULL || next->next == NULL)
			        {
			            prev->next = next;
			            break;
			        }
			 
			        // Change next of previous to next next
			        prev->next = next->next;
			        // Update previous and curr
			        prev = next;
			        cur = prev->next;
			    }
			}
			return head_train_index;
		} //else simply return head index since its only one in queue
		return Eb_q->train_index;
		break;
		case 'e':
			return 1;
		break;
		case 'W':
			return 1;
		break;
		case 'w':
			return 1;
		break;
	}
}

void printArray( Train** train_data ) {
		int i;
		for( i = 0; i < train_count; i++ ) {
		if(DEBUG) printf( "[DEBUG] [READING TRAIN %.3ld with PRI: %c, LT: %ld, CT: %ld, LIVE: %ld]\n", 
			(*train_data)[i].train_id, 
			(*train_data)[i].direction, 
			(*train_data)[i].loading_time, 
			(*train_data)[i].crossing_time,
			(*train_data)[i].live );
	}
}