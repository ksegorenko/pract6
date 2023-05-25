#include <iostream>
#include <stdio.h>
#include <mpi.h>
#include <pthreads.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define LIST_AMOUNT 1000
#define TASK_AMOUNT 1024

int size;
int rank;

int taskSend;
int taskLeft;
int* list;
int executedLists;
int executedThreadTasks;
int tasks;

bool sendThreadGetSignal = false;
bool executeThreadGetSignal = false;

pthread_mutex_t mutex;
pthread_mutex_t sendThreadMutex;
pthread_mutex_t executeThreadMutex;

pthread_cond_t tasksFinished;
pthread_cond_t newTasksAvailable;

pthread_t sendThread;
pthread_t recvThread;
pthread_t executeThread;

void* sendFunc(void* me) {
	MPI_Status status;
	pthread_mutex_lock(&sendThreadMutex);
	while (true) {
		// sendThreadМutex будет заблокирован пока не изменится значение условной ппеременной 
		pthread_cond_wait(&tasksFinished, &sendThreadMutex);
		sendThreadGetSignal = true;

		if (executedLists > LIST_AMOUNT) {
			pthread_mutex_unlock(&sendThreadMutex);
			pthread_exit(NULL);
		}
		bool sendRequestFlag = true;
		int recvTaskAmount;

		for (int i = 0; i < size; ++i) {
			if (i == rank) {
				continue;
			}
			MPI_Send(&sendRequestFlag, 1, MPI_C_BOOL, i, 0, MPI_COMM_WORLD);
			MPI_Recv(&recvTaskAmount, 1, MPI_INT, 1, 2, MPI_COMM_WORLD, &status);

			if (recvTaskAmount == 0) {
				continue;
			}
			MPI_Recv(&(list[0]), recvTaskAmount, MPI_INT, i, 1, MPI_COMM_WORLD, &status);
			break;
		}
		pthread_mutex_lock(&mutex);
		tasks = recvTaskAmount;
		executedThreadTasks = 0;

		while (executeThreadGetSignal == false)
			pthread_cond_signal(&newTasksAvailable);

		executeThreadGetSignal = false;
		pthread_mutex_unlock(&mutex);
	}
}


void* executeFunc(void* me) {
	int tasksPerProc = TASK_AMOUNT / size; // количество задач которые будут выполнены в каждом процессе
	list = new int[tasksPerProc];
	executedLists = 0;
	double iterTime = 0;
	double globalRes = 0;

	char threadRankPath[5]; // массив символов куда будет записываться путь к файлу, который будет создан
	threadRankPath[0] = rank + '0';
	memcpy(threadRankPath + 1, ".txt", 4);
	FILE* in = fopen(threadRankPath, "w"); // открываем файл для записи

	for (int listId = 0; listId < LIST_AMOUNT; ++listId) {
		double start
	}


}

int main(int argc, char** argv) {
	int provided;
	// в многопоточных программах используется вместо MPI_Init()
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	/* 1. &argc - то же, что и в MPI_Init
	*  2. &argv - то же, что и в MPI_Init
	*  3. MPI_THREAD_MULTIPLE - запрашиваемый уровень поддержки потоков:  программист ничего не обещает. Любой поток может вызывать MPI-функции независимо от других потоков.
	*  4. &provided - фактически предоставленный уровень поддержки потоков
	*/
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (provided != MPI_THREAD_MULTIPLE) {
		if (rank == 0) {
			printf("The requested level of thread support does not correspond to the actual level of thread support.\n");
		}
		MPI_Finalize();
		return 1;
	}

	// инициализация мьютексов перед их использованием
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&sendThreadMutex, NULL);
	pthread_mutex_init(&executeThreadMutex, NULL);
	/*
		1. мьютекс который хотим инициализировать
		2. атрибуты мьютекса. NULL указывает на то, что будут использоваться атрибуты по умолчанию
	*/

	// инициализация условных переменнных
	pthread_cond_init(&tasksFinished, NULL);
	pthread_cond_init(&newTasksAvailable, NULL);
	/*
		1. переменнная
		2. ее атрибуты. При значении NULL атрибуты будут выставлены по умолчанию
	*/

	double startTime = MPI_Wtime();

	pthread_attr_t attrs; // объект задающий атрибуты потока
	pthread_attr_init(&attrs); // инициализация атрибутов потока
	
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE); // установка свойства "присоединяемости" потока в атрибутах

	// порождение потоков
	pthread_create(&sendThread, &attrs, sendFunc, NULL);
	pthread_create(&recvThread, &attrs, recvFunc, NULL);
	pthread_create(&executeThread, &attrs, executeFunc, NULL);

	// освобождение ресурсов занятых атрибутами
	pthread_attr_destroy(&attrs);

	// для каждого присоединяемого потока один из других потоков должен явно вызвать эту функцию
	// в протвном случае завершившись поток освободит свои ресурсы, что может привести к утечкам памяти
	pthread_join(sendThread, NULL);
	pthread_join(receiveThread, NULL);
	pthread_join(executeThread, NULL);

	double endTime = MPI_Wtime();
	if (rank == 0) {
		printf("\nTime spent: %lf\n", endTime - startTime);
	}

	// освобождаем ресурсы занятые мьютексами и условными переменнными
	pthread_mutex_destroy(&mutex);
	pthread_mutex_destroy(&sendThreadMutex);
	pthread_mutex_destroy(&executeThreadMutex);
	pthread_cond_destroy(&tasksFinished);
	pthread_cond_destroy(&newTasksAvailable);

	MPI_Finalize();
	return 0;
}
