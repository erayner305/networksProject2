// client.cpp
//
//  Application for requesting file packets and downloading to local machine
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
#include <tuple>
#include <bits/stdc++.h>

int SEGMENT_SIZE = 512;
int TERMINATOR_BYTE = 1;
int CHECKSUM_SIZE = 4;
int PACKET_COUNT_SIZE = 4;
int INSTRUCTION_SIZE = 3;
int HEADER_SIZE = TERMINATOR_BYTE + CHECKSUM_SIZE + PACKET_COUNT_SIZE;
int DATA_SIZE = SEGMENT_SIZE - HEADER_SIZE;

char GET_INSTR[4] = "GET";
char ACK_INSTR[4] = "ACK";
char NAK_INSTR[4] = "NAK";


// buffToUint32
//
//  Convert a char[4] buffer to an uint32_t
//
uint32_t buffToUint32(char* buffer);


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

int main(int argc, char **argv) {

    // Poll for target IP address
    std::string server_address;
    std::cout << "Server IP Address: " << std::flush;
    std::getline(std::cin, server_address);

    // Create our network connection
    int sd;
    struct sockaddr_in server;
    socklen_t serAddrLen;
    serAddrLen = sizeof(server);

    sd = socket(AF_INET,SOCK_DGRAM,0);

    server.sin_family = AF_INET;
    server.sin_port = htons(SERV_PORT);
    server.sin_addr.s_addr = inet_addr(server_address.c_str());

    // Create working buffers and file data buffers
    int n;
    char message_buffer[SEGMENT_SIZE];
    std::string input_filename;
    std::ofstream downloaded_file;

    char file_data_buffer[DATA_SIZE];
    char packet_instruction[INSTRUCTION_SIZE];

    char packet_calculated_checksum_buff[CHECKSUM_SIZE];
    char packet_checksum_buff[CHECKSUM_SIZE];
    char packet_number_buff[PACKET_COUNT_SIZE];

    // Poll infinitely for the file we want to receive,
    // Construct it if it exists
    // Retry if the file does not exist on the server
    while(true) {
        std::cout << "File name to download: " << std::flush;
        std::getline(std::cin, input_filename);

        char packet[SEGMENT_SIZE];
        int expected_sequence_number = 0;
        
        //populate "packet" with GET and the file name
        strcpy(packet, GET_INSTR);
        memcpy(packet+4, input_filename.c_str(), input_filename.length());
        
        sendto(sd, packet, SEGMENT_SIZE, 0, (struct sockaddr*)&server, sizeof(server));

        // Receive a response from the server
        n = recvfrom(sd, message_buffer, SEGMENT_SIZE, 0, (struct sockaddr*)&server, &serAddrLen);
        memcpy(packet_instruction, &message_buffer, 4);
        
        // Print the response instruction, either ACK or ERR
        std::cout << "[Info] Server Response: " << packet_instruction << std::endl;

        // Declare a vector to hold all of our file data for sorting packets
        std::vector<std::tuple<int, std::vector<char>>> file_data_vector;

        // Check if we received an ACK instruction
        if (strcmp(packet_instruction, ACK_INSTR) == 0) {
            
            // File exists
            std::string downloaded_filename = input_filename.substr(input_filename.find_last_of("/\\") + 1);
            downloaded_file.open(downloaded_filename);

            // Clear our message buffer
            empty_buffer(message_buffer, SEGMENT_SIZE);

            for (;;) {
                // Get packet data
                n = recvfrom(sd, message_buffer, SEGMENT_SIZE, 0, (struct sockaddr*)&server, &serAddrLen);
                std::cout << "[Info] Got " << n << " bytes in Response..." << std::endl;


                // If the first byte of our buffer is \0, break out of our loop for receiving packets
                if (message_buffer[0] == '\0') {
                    break;
                }


                // Determine the checksum and packet number values
                std::memcpy(packet_checksum_buff, &message_buffer[1], 4);
                std::memcpy(packet_number_buff, &message_buffer[5], 4);

                uint32_t packet_number = buffToUint32(packet_number_buff);
                int packet_sequence_number = packet_number % 64;

                uint32_t packet_checksum = buffToUint32(packet_checksum_buff); 

                std::cout << "[Info] Got packet sequence number: " << packet_sequence_number << std::endl;
                std::cout << "[Info] Got packet checksum: " << packet_checksum << std::endl;


                // Determine if the incoming packet number is in the correct packet sequence
                if (packet_sequence_number == expected_sequence_number) {
                    std::cout << "[Info] Packet was in sequence!"<< std::endl;
                } else {
                    std::cout << "[Error] Packet was not in sequence!" << std::endl;
                    std::cout << "\tGot sequence number " << packet_sequence_number << std::endl;
                    std::cout << "\tExpected " << expected_sequence_number << std::endl;

                    // Send ACK
                    std::cout << "\tSending NAK Response..." << std::endl;
                    std::cout << "\tRequesting Packet #: " << expected_sequence_number << std::endl;
                    char nack_packet[5];
                    nack_packet[0] = (uint8_t)(expected_sequence_number);
                    std::memcpy(nack_packet + 1, NAK_INSTR, 4);
                    sendto(sd, nack_packet, 5, 0, (struct sockaddr*)&server, sizeof(server));

                    // Drop the Packet
                    continue;
                }


                // Get the raw file data from the message buffer
                std::memcpy(file_data_buffer, &message_buffer[9], 503);
                file_data_buffer[DATA_SIZE] = '\0';

                size_t len = strlen(file_data_buffer);
                char * newBuf = (char *)malloc(len);
                std::memcpy(newBuf, &file_data_buffer, len);


                // Generate the checksum from the packet and make sure it is
                // correct to the one included in the header
                generate_checksum(newBuf, packet_calculated_checksum_buff);
                uint32_t actual_checksum = buffToUint32(packet_calculated_checksum_buff);
                if(actual_checksum != packet_checksum) {
                    std::cout << "[Error] Packet Damaged" << std::endl;
                    std::cout << "\tRecieved: " << actual_checksum << std::endl;
                    std::cout << "\tExpected: " << packet_checksum << std::endl;

                    // Send NACK
                    std::cout << "\tSending NAK Response..." << std::endl;
                    std::cout << "\tRequesting Packet #: " << expected_sequence_number << std::endl;
                    char nack_packet[5];
                    nack_packet[0] = (uint8_t)expected_sequence_number;
                    std::memcpy(nack_packet + 1, NAK_INSTR, 4);
                    sendto(sd, nack_packet, 5, 0, (struct sockaddr*)&server, sizeof(server));

                    // Drop the packet
                    continue;
                } else {
                    std::cout << "[Info] Packet contents OK" << std::endl;
                }


                // Generate our packet tuple from the incoming packet
                std::vector<char> file_buffer_vector(newBuf, newBuf + len);
                std::tuple<uint32_t, std::vector<char>> packet_tuple (packet_number, file_buffer_vector);


                // Append the packet tuple to our vector of file data
                file_data_vector.push_back(packet_tuple);


                // Clear all our buffers
                empty_buffer(message_buffer, 4);
                empty_buffer(packet_calculated_checksum_buff, 4);
                empty_buffer(packet_checksum_buff, 4);
                empty_buffer(packet_number_buff, 4);

                expected_sequence_number = ((expected_sequence_number + 1) % 64);

                // Send ACK Response
                std::cout << "\tSending ACK Response..." << std::endl;
                std::cout << "\tRequesting Packet #: " << expected_sequence_number << std::endl;
                char ack_packet[5];
                ack_packet[0] = (uint8_t)(expected_sequence_number);
                std::memcpy(ack_packet + 1, ACK_INSTR, 4);
                sendto(sd, ack_packet, 5, 0, (struct sockaddr*)&server, sizeof(server));

            }
            
            std::cout << "[Info] Terminator packet received, end of transmission" << std::endl;

            std::cout << "[Info] Sorting recieved packet data..." << std::endl;

            // Sort the collected packets based on file packet numbers
            std::sort(file_data_vector.begin(), file_data_vector.end());

            std::cout << "[Info] Writing file data..." << std::endl;

            // Write the sorted file packets to the output file
            for (int i = 0; i< file_data_vector.size(); i++) {
                std::vector<char> data = std::get<1>(file_data_vector[i]);
                downloaded_file.write(&data[0], data.size());
            }
            
            // Close the file buffer
            downloaded_file.close();

            std::cout << "[Info] Downloaded file written to: " << downloaded_filename << std::endl;
            
        } else {
            std::cout << "[Error] File name does not exist on server, please try again" << std::endl;
        }

        // Clear our file and packet buffers
        empty_buffer(file_data_buffer, 504);
        empty_buffer(packet, SEGMENT_SIZE);
    }

    return 0;
}


// buffToUint32
//
//  Convert a char[4] buffer to an uint32_t
//
uint32_t buffToUint32(char* buffer)
{
    int a;
    memcpy(&a, buffer, sizeof( int ) );
    return a;
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
    //std::cout << "Checksum: " << sum << std::endl;
    memcpy(checksum_buffer, &sum, sizeof(sum));
}

