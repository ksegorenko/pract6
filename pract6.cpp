#include <iostream>
#include <pthread.h>
#include <mpi.h>
#include <cmath>
#include <deque>
#include <cstdlib>

#define MAX_ITERATIONS_COUNT 10
#define L 1000
#define TASKS_PER_PROC 100

std::deque<int> tasks; // разделяемые данные
bool tasksFinished; // разделяемые данные
pthread_mutex_t mutex;

int iterCounter = 0;
double globalRes = 0;
int tasksCount = 0;

int countWeight(int size, int rank, int index) {
    int result = abs(50 - (index % TASKS_PER_PROC)) * abs(rank - (iterCounter % size)) * L;
    return result;
}

void* workerFunc(void* attrs) {
    int taskWeight;
    bool running = true;
    while (running) {
        pthread_mutex_lock(&mutex);
        if (!tasks.empty()) {
            taskWeight = tasks.front(); // достаем первую задачу из начала очереди
            tasks.pop_front(); // удалаем из очереди задачу, которую достали
        }
        else {
            taskWeight = -1;
        }
        pthread_mutex_unlock(&mutex);

        if (taskWeight != -1) {
            double tempRes = 0;
            for (int i = 0; i < taskWeight; ++i) {
                tempRes += sin(i);
            }
            globalRes += tempRes;
            tasksCount++;
        }

        pthread_mutex_lock(&mutex);
        if (tasksFinished) {
            running = false;
        }
        pthread_mutex_unlock(&mutex);
    }
}

int DecideProcessRoot(int rank, bool want_be_root) {
    int root;
    int challenger;   // Don't want to be root -- send -1 to say it
    challenger = -1;
    if (want_be_root) {
        challenger = rank; // Want to be root       -- send his rank
    }
    MPI_Allreduce(&challenger, &root, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    return root;
}

int DecideRootPretendCount(bool want_be_root) {
    int root_pretend_count;
    int want_be_root_num = (int)want_be_root;
    MPI_Allreduce(&want_be_root_num, &root_pretend_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    return root_pretend_count;
}

void DelegateTask(int size, int rank, int worker_rank, int weight) {
    int delegated_tasks[size];

    MPI_Gather(&weight, 1, MPI_INT, delegated_tasks, 1, MPI_INT, worker_rank, MPI_COMM_WORLD);

    if (rank == worker_rank) {
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < size; ++i) {
            if (delegated_tasks[i] != -1) tasks.push_back(delegated_tasks[i]);
        }
        pthread_mutex_unlock(&mutex);
    }
}

void* managerFunc(void* attrs) {
    int size = ((int*)attrs)[0];
    int rank = ((int*)attrs)[1];
    int countOfTasksInDeque;
    int countOfRootProcesses = 0;
    
    // цикл выполняется пока все процессы не выполнят все задачи
    while (countOfRootProcesses != size) {
        pthread_mutex_lock(&mutex);
        countOfTasksInDeque = (int)tasks.size();
        printf("Current count of tasks in deque: %d\n", countOfTasksInDeque);
        bool want_be_root = (bool)(countOfTasksInDeque == 0);
        int actual_root = DecideProcessRoot(rank, want_be_root);
        if (actual_root != -1) { // какой-то процесс хочет получить больше заданий
            countOfRootProcesses = DecideRootPretendCount(want_be_root);

            int delegate_task_weight = -1; // don't want to delegate
            pthread_mutex_lock(&mutex);
            if (!want_be_root && !tasks.empty()) {
                delegate_task_weight = tasks.back();
                tasks.pop_back();
            }
            pthread_mutex_unlock(&mutex);
            DelegateTask(size, rank, actual_root, delegate_task_weight);
        }
    }
    // сообщаем worker'у что все задачи выполнены
    pthread_mutex_lock(&mutex);
    tasksFinished = true;
    pthread_mutex_unlock(&mutex);
}


int main(int argc, char* argv[]) {
    int rank; 
    int size;
    double startTime;
    double endTime;
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided != MPI_THREAD_MULTIPLE) {
        if (rank == 0) {
            printf("The requested level of thread support does not correspond to the actual level of thread support.\n");
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    pthread_mutex_init(&mutex, NULL);

    int startTasksBorder = TASKS_PER_PROC * rank;
    int endTasksBorder = startTasksBorder + TASKS_PER_PROC;

    // атрибуты необходимые для функции meneger'a
    int size_rank[2] = {size, rank};

    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
    int maxImbalanceProportion = 0;

    if (rank == 0) {
        startTime = MPI_Wtime();
    }

    for (int i = 0; i < MAX_ITERATIONS_COUNT; ++i) {
        for (int j = startTasksBorder; j < endTasksBorder; ++j) {
            tasks.push_back(countWeight(size, rank, j));
        }
        tasksFinished = false;

        pthread_t worker;
        pthread_t manager;

        pthread_create(&worker, &attrs, workerFunc, NULL);
        pthread_create(&manager, &attrs, managerFunc, (void*)size_rank);

        double iterationStartTime = MPI_Wtime();

        pthread_join(worker, NULL);
        pthread_join(manager, NULL);

        double iterationEndTime = MPI_Wtime();
        double totalIterationTime = iterationEndTime - iterationStartTime;

        double maxIterationTime;
        double minIterationTime;

        MPI_Reduce(&totalIterationTime, &maxIterationTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&totalIterationTime, &minIterationTime, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

        // вывод результатов итераций
        for (int j = 0; j < size; ++j) {
            MPI_Barrier(MPI_COMM_WORLD);
            if (j == rank) {
                printf("| %2d | %2d | Tasks computed: %10d        |\n", iterCounter, rank, tasksCount);
                printf("| %2d | %2d | Global result: %11lf        |\n", iterCounter, rank, globalRes);
                printf("| %2d | %2d | Total iteration time: %11lf |\n", iterCounter, rank, totalIterationTime);
                printf("+-----+-----+-----------------------------+\n");
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            double imbalanceTime = maxIterationTime - minIterationTime;
            double imbalanceProportion = imbalanceTime / maxIterationTime * 100.0;
            if (imbalanceProportion > maxImbalanceProportion) {
                maxImbalanceProportion = imbalanceProportion;
            }
            printf("| %2d | Time disbalance: %14lf          |\n", iterCounter, imbalanceTime);
            printf("| %2d | Disbalance prop: %12lf %        |\n", iterCounter, imbalanceProportion);
            printf("+-----+---------------------------------+\n");
        }
        tasksCount = 0;
        globalRes = 0;

        iterCounter++;
    }

    // Destroy attributes
    pthread_attr_destroy(&attrs);
    pthread_mutex_destroy(&mutex);

    if (rank == 0) {
        endTime = MPI_Wtime();
        printf("Time spent: %lf\n", endTime - startTime);
        printf("Max disbalance proportion: %lf\n", maxImbalanceProportion);
    }

    MPI_Finalize();
    return 0;
}
