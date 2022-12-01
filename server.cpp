// client.cpp
//
//  Application for serving file packets and sending data to client machine
//
// Authors:
//  Garrett Dickinson
//  Logan Sayle
//  Easton Rayner

#include "unp.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>

int SEGMENT_SIZE = 512;
int TERMINATOR_BYTE = 1;
int CHECKSUM_SIZE = 4;
int PACKET_COUNT_SIZE = 4;
int INSTRUCTION_SIZE = 3;
int HEADER_SIZE = TERMINATOR_BYTE + CHECKSUM_SIZE + PACKET_COUNT_SIZE;
int DATA_SIZE = SEGMENT_SIZE - HEADER_SIZE;

char TERM_OKAY = '1';
char GET_INSTR[4] = "GET";
char ACK_INSTR[4] = "ACK";
char ERR_INSTR[4] = "ERR";

std::string input_packet_loss_rate;
std::string input_packet_damage_rate;

float packet_loss_rate;
float packet_damage_rate;

// gremlins
// 
//  Given a char buffer, corruption chance, and loss chance, mutate the packets data to create an
//  invalid packet.
//
int gremlins(char buffer[], double corruptionChance, double lossChance, double delayChance, int delayTime);

// empty_buffer
//
//  Set all cells of a char buffer to NUL character
//
void empty_buffer(char buffer[], int size);


// generate_checksum
// 
//  Given a char buffer, generate a checksum and return the value in a provided char[4] buffer
//
void generate_checksum(char input_buffer[], char output_buffer[]);


// generate_packet_num
// 
//  Given a uint32_t packet number, return the value in a provided char[4] buffer
//
void generate_packet_num(uint32_t packet_num, char packet_num_buffer[]);

int main() {

    // Start our network connection code

    int n, sd, client_fd;
    struct sockaddr_in server;
    socklen_t serverLen = sizeof(server);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(SERV_PORT);

    sd = socket(AF_INET, SOCK_DGRAM, 0);

    // Bind the server to the socket
    bind(sd, (struct sockaddr *)&server, sizeof(server));

    listen(sd, 10);

    struct sockaddr_storage client_addr;
    socklen_t client_size = sizeof(client_addr);
    client_fd = accept(sd, (struct sockaddr *)&client_addr, &client_size);

    std::cout << "Enter packet loss chance: " << std::flush;
    std::getline(std::cin, input_packet_loss_rate);
    packet_loss_rate = std::stof(input_packet_loss_rate);

    std::cout << "Enter packet damage chance: " << std::flush;
    std::getline(std::cin, input_packet_damage_rate);
    packet_damage_rate = std::stof(input_packet_damage_rate);

    std::cout << "Ready" << std::endl;


    // Integer to keep track of our packet counts
    int packet_count = 0;
    
    // Define char buffers  based on our segment size
    char data_buffer[DATA_SIZE];
    
    // Define an empty buffer for our generated checksum
    char checksum_buffer[CHECKSUM_SIZE];

    // Define an empty buffer for our generated checksum
    char packet_count_buffer[PACKET_COUNT_SIZE];   

    // Define our empty packet
    char packet[SEGMENT_SIZE];

    // Define our buffer to store our incoming message
    char message_buffer[SEGMENT_SIZE];

    // Define a buffer to store the GET instruction of the message 
    char instruction_buffer[4];

    // Define a buffer to store the filename passed in the GET packet
    char filename_buffer[SEGMENT_SIZE - INSTRUCTION_SIZE];

    // Input string to open
    std::string input_name;

    // File in stream
    std::ifstream file_in;

    // Poll infinitely for requests from the client
    while (true) {
        std::cout << "Waiting for request" << std::endl;

        // Capture the recieved message bytes to the message buffer
        n = recvfrom(sd, message_buffer, SEGMENT_SIZE, 0, (struct sockaddr *)&server, &serverLen);
        
        // Pull the instruction out of the message buffer into the instruction buffer 
        std::copy(message_buffer, message_buffer+4, instruction_buffer);

        // Check if the instruction is a GET request
        if (strcmp(instruction_buffer, GET_INSTR) == 0) {

            // Copy the file name from the request to the filename char buffer
            std::copy(message_buffer+4, message_buffer+SEGMENT_SIZE, filename_buffer);

            // Cast the buffer to a std::string
            std::string target_filename = std::string(filename_buffer);

            // Open the targe file
            file_in.open(target_filename.c_str(), std::ios_base::binary);

            // Check if the file exists
            if (file_in) {

                // Send an ACK packet to the client
                sendto(sd, ACK_INSTR, 4, 0, (struct sockaddr*)&server, sizeof(server));

                // File requested exists, send all of the packets for the file
                while(!file_in.eof()){
                    empty_buffer(packet, SEGMENT_SIZE);
                    empty_buffer(data_buffer, DATA_SIZE);
                    empty_buffer(checksum_buffer, CHECKSUM_SIZE);

                    file_in.read(data_buffer, DATA_SIZE);

                    // Generate our checksum using our buffer
                    generate_checksum(data_buffer, checksum_buffer);
                    generate_packet_num(++packet_count, packet_count_buffer);

                    std::memcpy(packet, &TERM_OKAY, TERMINATOR_BYTE);    
                    std::memcpy(packet+TERMINATOR_BYTE, &checksum_buffer, CHECKSUM_SIZE);
                    std::memcpy(packet+TERMINATOR_BYTE+CHECKSUM_SIZE, &packet_count_buffer, PACKET_COUNT_SIZE);
                    std::memcpy(packet+HEADER_SIZE, &data_buffer, DATA_SIZE);

                    gremlins()

                    // Send packet to client
                    sendto(sd, packet, SEGMENT_SIZE, 0, (struct sockaddr *)&server, sizeof(server));

                    // Sleep in the event of packet overflow
                    usleep(100);
                }

                // Send terminal \0 byte to the client marking end of tranmission
                sendto(sd, "\0", 1, 0, (struct sockaddr *)&server, sizeof(server));

            } else {

                // Packet does not exist, send an ERR packet to the client
                std::cout << "[Error] Received request for file " << target_filename << " that does not exist" << std::endl;
                sendto(sd, ERR_INSTR, 4, 0, (struct sockaddr*)&server, sizeof(server));

            }

            // Clear all of our working buffers
            empty_buffer(packet, SEGMENT_SIZE);
            empty_buffer(message_buffer, SEGMENT_SIZE);
            empty_buffer(data_buffer, DATA_SIZE);
            
            // Close our file and reset our packet counts
            file_in.close();
            packet_count = 0;
        }
    }
    
    close(sd);

    return 0;
}


// empty_buffer
//
//  Set all cells of a char buffer to NUL character
//
void empty_buffer(char buffer[], int size) {
    for (int i = 0; i < size; i++) {
        buffer[i] = '\0';
    }
}


// generate_packet_num
// 
//  Given a uint32_t packet number, return the value in a provided char[4] buffer
//
void generate_packet_num(uint32_t packet_num, char packet_num_buffer[]) {
    std::cout << "Generated Packet Number: " << packet_num << std::endl;
    memcpy(packet_num_buffer, &packet_num, sizeof(packet_num));
}


// generate_checksum
// 
//  Given a char buffer, generate a checksum and return the value in a provided char[4] buffer
//
void generate_checksum(char data_buffer[], char checksum_buffer[]) {
    uint32_t sum = 0;
    for(int i = 0; i < DATA_SIZE; i++) {
        if (data_buffer[i] == '\0') break;
        sum += data_buffer[i];
    }
    std::cout << "Generated Checksum: " << sum << std::endl;
    memcpy(checksum_buffer, &sum, sizeof(sum));
}

// gremlins
// 
//  Given a char buffer, corruption chance, and loss chance, mutate the packets data to create an
//  invalid packet.
//
int gremlins(char buffer[], double corruptionChance, double lossChance, double delayChance, int delayTime){
    double randomNum;
    int randomByte;
    srand(rand()*time(NULL));

    //Error Checking.
    if (corruptionChance > 1 || corruptionChance < 0 || lossChance > 1 || lossChance < 0) { 
        return -1;
    } 

    double rand_losschance = (double) rand() / RAND_MAX;

    if(rand_losschance < lossChance){ //Checks for loss of packet
        std::cout << "[Gremlin] Packet was lost" << std::endl;
        return 1;
    }
    else if ((double) rand()/RAND_MAX < delayChance){ //Checks for delay of packet
        
    }
    else if ((double) rand()/RAND_MAX < corruptionChance) { //Checks for corruption of packet
        randomNum = (double) rand()/RAND_MAX;
        if(randomNum <= 0.7){ //70% only one packet is affected
            std::cout << "[Gremlin] 1/3 bytes were affected" << std::endl;
            randomByte = rand() % 512;
            buffer[randomByte] = '1';
        }
        
        if(randomNum <= 0.2){ //20% chance two packets are affected
            std::cout << "[Gremlin] 2/3 bytes were affected" << std::endl;
            randomByte = rand() % 512;
            buffer[randomByte] = '1';
        }

        if(randomNum <= 0.1){ //10% chance three packets are affected
            std::cout << "[Gremlin] 3/3 bytes were affected" << std::endl;
            randomByte = rand() % 512;
            buffer[randomByte] = '1';
        }
        return 2;
    }

    return 0;
 
}