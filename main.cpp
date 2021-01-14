#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <thread>
#include <pthread.h>

#include <mysql.h>

#include <vector>
#include <sstream>
#define PORT 54000

struct mysql_data{
    const char* hostname;
    const char* username;
    const char* password;
    const char* database;
    unsigned int port;
    const char* unixsocket;
    unsigned long clientflag;
};
struct owe_data{
    std::string id, debtor, creditor, deadline;
    float amount;
};
struct sockaddr_in;
void waitForRequests(int serverSocket, std::vector<int> clients, MYSQL* conn);
void getAdditionDetails(std::string details, std::string& outDebtor, std::string& outCreditor, std::string& outAmount, std::string& outDeadline, std::string& outId);
static void talk(int clientSocket, MYSQL* conn);

void debugPrint(std::string debtor, std::string credtitor, std::string amount, std::string deadline, std::string id);


MYSQL* connect_to_database();
bool insert_owe(MYSQL* conn, owe_data* owe);

fd_set fr, fw, fe;

int main()
{
    int serverSocket;
    sockaddr_in srv;
    std::vector<int> clients;

    MYSQL* conn = connect_to_database();
    if(conn)
    {
        std::cout<<"Connection was succesful"<<std::endl;
    }else{
        std::cout<<"Connection was unsuccesful"<<conn<<std::endl;
    }


    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(serverSocket < 0)
    {
        std::cout<<"Failed to create socket"<<std::endl;
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, "0.0.0.0", &srv.sin_addr);
    memset(srv.sin_zero, 0, 8);

    if(bind(serverSocket, (sockaddr*)&srv, sizeof(sockaddr)) < 0)
    {
        std::cout<<"Bind failed"<<std::endl;
        return -1;
    }else{
        std::cout<<"Bind succeeded"<<std::endl;
    }

    if(listen(serverSocket, 5) < 0)
    {
        std::cout<<"Listen failed"<<std::endl;
    }else{
        std::cout<<"Listen succeeded"<<std::endl;
    }
    while(true)
    {
        waitForRequests(serverSocket, clients, conn);
    }
}

void waitForRequests(int serverSocket, std::vector<int> clients, MYSQL* conn)
{
        FD_ZERO(&fr);
        FD_ZERO(&fw);
        FD_ZERO(&fe);

        FD_SET(serverSocket, &fr);
        FD_SET(serverSocket, &fe);
        for(int client : clients)
        {
            FD_SET(client, &fr);
            FD_SET(client, &fe);
        }

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        select(serverSocket+1, &fr, &fw, &fe, &timeout);
        //NEW REQESTS
        if(FD_ISSET(serverSocket, &fr))
        {
            std::cout<<"New connection request"<<std::endl;

            //There is some new request
            int nLen = sizeof(struct sockaddr);
            auto clientSocket = accept(serverSocket, NULL, (socklen_t*)&nLen);
            if(clientSocket > 0)
            {
                std::cout<<"Successfully accepted client: "<<clientSocket<<std::endl;
                clients.push_back(clientSocket);
                //Create thread for further communication
                std::thread clientThread(talk, clientSocket, conn);
                clientThread.detach();
            }
        }
}

static void talk(int clientSocket, MYSQL* conn)
{
    std::cout<<"Talk thread started with client socket: "<<clientSocket<<std::endl;
    while(true)
    {
        FD_SET(clientSocket, &fr);
        FD_SET(clientSocket, &fe);
        if(FD_ISSET(clientSocket, &fr))
        {
            std::cout<<"New reqest isset"<<std::endl;
            char buff[256];
            int nRet = recv(clientSocket, &buff, 256, 0);
            if (nRet>0)
            {
                std::string msg(buff);
                if(msg == "add")
                {
                    recv(clientSocket, buff, 256, 0);
                    std::string details(buff);
                    std::string debtor, creditor, amount, deadline, id;
                    getAdditionDetails(details, debtor, creditor, amount, deadline, id);
                    debugPrint(debtor, creditor, amount, deadline, id);
                    float f_amount = std::stof(amount);
                    owe_data* owe = new owe_data();
                    owe->id = id;
                    owe->debtor = debtor;
                    owe->creditor = creditor;
                    owe->amount = f_amount;
                    owe->deadline = deadline;
                    if(insert_owe(conn, owe))
                    {
                        send(clientSocket, "Owe Inserted successfully\0", 26, 0);
                    }else{
                        send(clientSocket, "Failed To Insert owe, maybe you should change identifier\0", 57, 0);
                    }
                }else if(msg == "remove")
                {
                    memset(buff, 0, 256);
                    std::cout<<"Remove request"<<std::endl;
                    recv(clientSocket, buff, 256, 0);
                }
            }else{
                std::cout<<"Client with number "<<clientSocket<<" closed the connection"<<std::endl;
                break;
            }

        }
    }
}
void getAdditionDetails(std::string details, std::string& outDebtor, std::string& outCreditor, std::string& outAmount, std::string& outDeadline, std::string& outId)
{
    std::vector<int> semicolons;
    int len = details.length();
    int index = 0;
    for(char a : details)
    {
        if(a == ';')
        {
            semicolons.push_back(index);
        }
        index++;
    }

    outDebtor = details.substr(0, semicolons[0]);
    outCreditor = details.substr(semicolons[0]+1, semicolons[1]-semicolons[0]-1);
    outAmount = details.substr(semicolons[1]+1, semicolons[2]-semicolons[1]-1);
    outDeadline = details.substr(semicolons[2]+1, semicolons[3] - semicolons[2] - 1);
    outId = details.substr(semicolons[3]+1, details.length() - semicolons[3] - 1);
}

void debugPrint(std::string debtor, std::string credtitor, std::string amount, std::string deadline, std::string id)
{
    std::cout<<"Debtor: "<<debtor<<std::endl;
    std::cout<<"Creditor: "<<credtitor<<std::endl;
    std::cout<<"Amount: "<<amount<<std::endl;
    std::cout<<"Deadline: "<<deadline<<std::endl;
    std::cout<<"Identifier: "<<id<<std::endl;
}

MYSQL* connect_to_database()
{
    MYSQL* conn;
    mysql_data data;
    data.hostname = "127.0.0.1";
    data.username = "root";
    data.password = "";
    data.database = "owes";
    data.port = 3306;
    data.unixsocket = NULL;
    data.clientflag = 0;

    conn = mysql_init(0);
    conn = mysql_real_connect(conn, data.hostname, data.username, data.password, data.database, data.port, data.unixsocket, data.clientflag);

    return conn;
}

bool insert_owe(MYSQL* conn, owe_data* owe)
{
    std::stringstream ss;
    int qstate = 0;
    ss<<"INSERT INTO owe (id, debtor, creditor, amount, deadline) VALUES ('"<<owe->id<<"', '"<<owe->debtor<<"', '"<<owe->creditor<<"', '"<<owe->amount<<"', '"<<owe->deadline<<"')";
    std::string query = ss.str();
    const char* q = query.c_str();
    qstate = mysql_query(conn, q);
    if(qstate == 0)
    {
        std::cout<<"Record Inserted"<<std::endl;
        return true;
    }else{
        return false;
        std::cout<<"Record insertion failed"<<std::endl;
    }
}
