#include <iostream>
#include <stdio.h>
#include <mpi.h>
#include <pthread.h>
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

// функция отправляющая запрос на выполнение задач другим процессам в сети MPI
void* sendFunc(void* me) {
	MPI_Status status;
	// захватываем мьютекс для потока отправки запросов
	pthread_mutex_lock(&sendThreadMutex);
	while (true) {
		// поток ожидает появления сигнала о завершении задач(от другого процесса)
		pthread_cond_wait(&tasksFinished, &sendThreadMutex);
		// если получен сигнал о завершении задач, то устанавливается флаг sendThreadGetSignal
		sendThreadGetSignal = true;

		if (executedLists > LIST_AMOUNT) {
			// если все списки задач были выполнены то поток освобождает мьютекс и завершает своё выполнение
			pthread_mutex_unlock(&sendThreadMutex);
			pthread_exit(NULL);
		}
		bool sendRequestFlag = true;
		int recvTaskAmount;

		for (int i = 0; i < size; ++i) {
			if (i == rank) {
				continue;
			}
			// отправка запроса на выполнение задач другим процессам в сети MPI
			MPI_Send(&sendRequestFlag, 1, MPI_C_BOOL, i, 0, MPI_COMM_WORLD);
			// получение ответа о количестве задач
			MPI_Recv(&recvTaskAmount, 1, MPI_INT, 1, 2, MPI_COMM_WORLD, &status);
			// при получении ответа проверяется что получаемый результат не равен нулю
			if (recvTaskAmount == 0) {
				continue;
			}
			// если количество задач не равно нулю то принимаются данные с полученными задачами из другого потока
			MPI_Recv(&(list[0]), recvTaskAmount, MPI_INT, i, 1, MPI_COMM_WORLD, &status);
			break;
		}
		// захватывается мьютекс чтобы осуществить доступ к общему списку задач
		pthread_mutex_lock(&mutex);
		// передается информация о количестве полученных задач
		tasks = recvTaskAmount;
		// обнуляется счетчик выполненных задач
		executedThreadTasks = 0;

		// Если же счетчик выполненных задач другим потоком обнулен, то поток получает сигнал о постановке новых задач,
		// устанавливает executeThreadGetSignal в false и освобождает мьютекс.
		while (executeThreadGetSignal == false)
			pthread_cond_signal(&newTasksAvailable);

		executeThreadGetSignal = false;
		pthread_mutex_unlock(&mutex);
	}
}

// функция, принимающая запросы на выполнение задач от других процессов в сети MPI
void* recvFunc(void* me) {
	MPI_Status status;
	while (true) {
		// поток ожидает получения сигнала запроса на выполнение задач от других процессов в сети MPI
		bool recvRequestFlag = false;
		//  Если получен сигнал о запросе на выполнение задач от другого процесса, то поток устанавливает флаг recvRequestFlag в true и получает идентификатор процесса-отправителя.
		MPI_Recv(&recvRequestFlag, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

		// если получен сигнал о завершении работы потока(ноль) то поток завершает свое выполнение
		if (recvRequestFlag == 0) {
			pthread_exit(NULL);
		}
		// Захватывается мьютекс для работы с общим списком задач.
		pthread_mutex_lock(&mutex);
		// вычисляется количество задач которые должны быть отправлены процессу-получателю
		taskSend = (tasks - executedThreadTasks) / 2;
		// вычисляется количество задач которые останутся необработанными для других процессов
		taskLeft = (tasks - executedThreadTasks + 1) / 2;

		// вычисленное количество задач отправляется процессу-получателю
		MPI_Send(&taskSend, 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD);

		// если количество задач которые должны быть отправлены не равно нулю, то данные с задачами отправляются в форме массива
		if (taskSend != 0) {
			MPI_Send(&(list[executedThreadTasks + taskLeft]), taskSend, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
			// обновление количества задач которые остались для других потокв и процессов
			tasks = executedThreadTasks + taskLeft;
		}
		// освобождае6м мьютекс управляющий доступом к общему списку задач
		pthread_mutex_unlock(&mutex);
	}
	// поток продолжает ожидать другие запросы
}

void* executeFunc(void* me) {
	int tasksPerProc = TASK_AMOUNT / size; // количество задач которые будут выполнены в каждом процессе
	list = new int[tasksPerProc];
	executedLists = 0;
	double iterTime = 0;
	double globalRes = 0;

	for (int listId = 0; listId < LIST_AMOUNT; ++listId) {
		double start = MPI_Wtime();
		for (int i = 0; i < tasksPerProc; ++i) {
			// заполняем массив list задачами, которые будут выполнены в этом цикле
			list[i] = i * abs(rank - (executedLists % size)) * 64;
		}
		tasks = tasksPerProc;
		executedThreadTasks = 0;
		int totalExecutedTasks = 0;

		// запускаем бесконечный цикл который выполняется пока все задачи не будут выполнены
		while (true) {
			if (tasks == 0) {
				break;
			}
			// в цикле увеличиваем значение globalRes на cos(i) и увеличиваем значение executedThreadsTasks
			for (int taskId = 0; taskId < tasks; ++taskId) {
				pthread_mutex_lock(&mutex);
				executedThreadTasks++;
				pthread_mutex_unlock(&mutex);

				for (int i = 0; i < list[taskId]; ++i) {
					globalRes += cos(i);
				}
			}
			// блокируем поток и ожидаем сигнала от другого потока
			totalExecutedTasks += executedThreadTasks;
			pthread_mutex_lock(&executeThreadMutex);

			while (sendThreadGetSignal == false) {
				pthread_cond_signal(&tasksFinished);
			}
			sendThreadGetSignal = false;
			pthread_cond_wait(&newTasksAvailable, &executeThreadMutex);
			executeThreadGetSignal = true;
			pthread_mutex_unlock(&executeThreadMutex);
		}
		// запсываем результаты в файл и увеличиваем значение executedLists
		double end = MPI_Wtime();
		iterTime = end - start;
		int iterCounter = executedLists;
		printf("%d - %d - %d - %f - %f\n", rank, iterCounter, totalExecutedTasks, globalRes, iterTime);
		executedLists++;
		// блокируем все процессы до тех пор пока они не достигнут этой точки в коде
		MPI_Barrier(MPI_COMM_WORLD);
	}
	// устанавливаем значение recvRequestFlag и отправляем его в процесс с номером rank 
	pthread_mutex_lock(&mutex);
	bool recvRequestFlag = false;
	MPI_Send(&recvRequestFlag, 1, MPI_C_BOOL, rank, 0, MPI_COMM_WORLD);
	executedLists++;
	pthread_cond_signal(&tasksFinished);
	pthread_mutex_unlock(&mutex);
	// закрываем файл и завершаем работу
	free(list);

	pthread_exit(NULL);
}

int main(int argc, char** argv) {
	// переменная отвечающая за уровень поддержки потоков
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
	// если запрашиваемый уровень поддержки потоков и фактический не совпадают выбрасывается ошибка
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
	pthread_create(&sendThread, &attrs, sendFunc, NULL); // sendThread будет выполнять функцию sendFunc
	pthread_create(&recvThread, &attrs, recvFunc, NULL); // recvThread будет выполнять функцию recvFunc
	pthread_create(&executeThread, &attrs, executeFunc, NULL); // executeThread будет выполнять функцю executeFunc

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
