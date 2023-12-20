#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

const char processName = 'B';
const char EOS = '\0';
const int MESSAGE_SIZE = 15;
int msgrecvd = 0;
int msgrcvchunks = 0;
int msgsent = 0;
int msgsentchunks = 0;
std::chrono::duration<double, std::milli> msgWaitTm;


struct SharedMemory {
    char message_ready;
    bool have_more;
    bool exit_signal;
    char message_text[MESSAGE_SIZE];
};

// Functions for interacting with the semaphore
void waitSemaphore(int semid) {
    struct sembuf semaphoreOperation = {0, -1, 0};
    semop(semid, &semaphoreOperation, 1);
}

void signalSemaphore(int semid) {
    struct sembuf semaphoreOperation = {0, 1, 0};
    semop(semid, &semaphoreOperation, 1);
}

// Functions for creating and accessing shared memory
int createSharedMemory(key_t key, size_t size) {
    int shmid = shmget(key, size, IPC_CREAT | 0666);
    return shmid;
}

void* attachSharedMemory(int shmid) {
    return shmat(shmid, NULL, 0);
}

void detachSharedMemory(const void* sharedMemory) {
    shmdt(sharedMemory);
}

void removeSharedMemory(int shmid) {
    shmctl(shmid, IPC_RMID, NULL);
}

// Functions for creating and accessing the semaphore
int createSemaphore(key_t key, int initialValue) {
    std::cout << "Creating semphore: " << key << std::endl;
    int semid = semget(key, 1, IPC_CREAT | 0666);
    if(semctl(semid, 0, SETVAL, initialValue) == -1) {
        std::cerr << "Failed to initialize semaphore" << std::endl;
        semctl(semid, 0, IPC_RMID);
        return -1;
    }
    return semid;
}

int main() {


    key_t key = ftok("message_queueAB", 65);

    if(key == -1) {
        std::cerr << "Failed to create key: " << errno << std::endl;
        return -1;
    }

    // Create a semaphore
    int semid = createSemaphore(key, 1);

    if(semid == -1) {
        std::cerr << "Failed to create semaphore" << std::endl;
        return -1;
    }

    // Create shared memory
    int shmid = createSharedMemory(key, sizeof(SharedMemory));

    // Attach shared memory
    SharedMemory* sharedMemory = static_cast<SharedMemory*>(attachSharedMemory(shmid));

    // Initialize shared memory variables
    sharedMemory->message_ready = EOS;
    sharedMemory->exit_signal = false;

    std::stringstream fileName;
    fileName << "process" << processName << ".txt";
    std::cout << "Starting Process: " << processName << std::endl;
    std::ofstream outputFile(fileName.str());

    // Function to manage the threads of Process A
    auto processA = [&]() {
        std::string message;

        auto startTm = std::chrono::high_resolution_clock::now();
        while (true) {
            waitSemaphore(semid);

            if (sharedMemory->exit_signal) {
                std::cout << "Process " << processName << " terminated.\n";
                signalSemaphore(semid);
                break;
            }

            if(sharedMemory->message_ready == EOS) {
                sharedMemory->message_text[0] = EOS;
                signalSemaphore(semid);
                continue;
            }

            if (sharedMemory->message_ready == processName) {
                signalSemaphore(semid);
                continue;
            }
            // Append buffer and empty shared message
            if(sharedMemory->message_text[0] != EOS) {
                //Capture the time for first message
                if(message.length() == 0) {
                    msgWaitTm += std::chrono::high_resolution_clock::now() - startTm;
                }
                message += sharedMemory->message_text;
                sharedMemory->message_text[0] = EOS;
                msgrcvchunks++;
            } 

            // if we dont have more then break
            if(!sharedMemory->have_more) {
                //Release the buffer
                std::cout << "Process " << processName 
                          << ": Received from: " << sharedMemory->message_ready 
                          << ": '" << message << "'" << std::endl;
                outputFile << message << std::endl;
                sharedMemory->message_ready = EOS;
                message = "";
                startTm = std::chrono::high_resolution_clock::now();
                msgrecvd++;
            }
            signalSemaphore(semid);
        }
    };

    // Create a thread
    std::thread threadA(processA);

    // Function to manage the input of Process A
    std::string input;
    while (true) {
        std::cout << "Enter a message (or #BYE# to terminate): ";
        std::getline(std::cin, input);
        //input = input.replace(input.begin(),input.end(),"\r\n","");
        //input = input.replace(input.begin(),input.end(),"\n","");

        waitSemaphore(semid);
        if (input == "#BYE#" || sharedMemory->exit_signal) {
            sharedMemory->exit_signal = true;
            signalSemaphore(semid);
            break;
        }
        signalSemaphore(semid);

        // Dont process empty input
        if(input.length() == 0) {
            continue;
        }

        // Write the data in chunks
        int writeln = 0;
        while(input.length() > 0) {
            //std::cout << "waiting for semaphore " << sharedMemory->message_ready << " message: " << sharedMemory->message_text << " tosend" << input << std::endl;
            waitSemaphore(semid);
            if(sharedMemory->message_ready == EOS) {
                //Aquire the message buffer.
                sharedMemory->message_ready = processName;
                sharedMemory->message_text[0] = EOS;
            }
            // Check if buffer is allocated and there is space to write
            if((sharedMemory->message_ready == processName && sharedMemory->message_text[0] == EOS)) {
                // Store the message in shared memory
                if (input.length() > MESSAGE_SIZE) {
                    // Write at most n - 1 bytes as last byte is reserved for null character
                    writeln = MESSAGE_SIZE - 1;
                } else {
                    writeln = input.length();
                }
                strncpy(sharedMemory->message_text, input.c_str(), writeln);
                sharedMemory->message_text[writeln] = EOS;
                input = input.substr(writeln);
                sharedMemory->have_more = (input.length() > 0) ;
                msgsentchunks++;
            }
            signalSemaphore(semid);
        }
        msgsent++;
    }

    // Wait for the Process A thread to complete
    threadA.join();

    // Detach shared memory and remove both shared memory and semaphore
    detachSharedMemory(sharedMemory);
    removeSharedMemory(shmid);
    semctl(semid, 0, IPC_RMID);

    // Print the statistics
    std::cout << "Messages Received: " << msgrecvd << std::endl;
    std::cout << "Messages Sent: " << msgsent << std::endl;
    std::cout << "Average Chunks Received per Message: " << (double) msgrcvchunks / msgrecvd << std::endl;
    std::cout << "Average Chunks Sent per Message: " << (double) msgsentchunks / msgsent << std::endl;
    std::cout << "Average Waiting Time for Receipt of First Piece: " << msgWaitTm.count() / msgrecvd << std::endl;
    return 0;
}

