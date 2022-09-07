# File Storage Server
Final project for Operative System course [@Unipisa](https://github.com/Unipisa)

## Overview
The project is an implementation of a Client-Server application, a *File Storage Server*, in which files are stored in main memory. The storage has fixed capacity, defined in the configuration file, passed during server startup. Each client, through a request-response protocol, can send multiple requests to the server, to obtain or change the storage status. The storage implements a file ejection mechanism when the limits of the number of files and amount of bytes that can be stored are exceeded.

More details (in italian) in [Report.pdf](https://github.com/fram112/file-storage-server/blob/04c12257844d7c4e82ed775527b82ebc2e764197/Report.pdf)

## Prerequisites
The project uses `make` to build source and `valgrind` for memory leak checks. Make sure that this tools are installed.

## Build and usage
Compile the source code and get client and server executables:
```
$ make
```
Start the server:
```
$ bin/server -f <configfile>
```
Start the client:
```
$ bin/client -a <clientusername> -f <serversocket> [options]
```
Delete client and server executables:
```
$ make clean
```
Delete all temporary files, including executables:
```
$ make cleanall
```

## Tests
Test files and scripts are provided, to evaluate the correctness and performance of *File Storage Server* operations.

### Test 1
The first test performs all the operations provided by the storage and checks with `valgrind` the absence of memory leaks. To run the test 1:
```
$ make test1
```
### Test 2
The second test checks the correct functioning of the file replacement algorithm. To run the test 2:
```
$ make test2
```
### Test 3
The latter performs a stress test on the *File Storage Server*, continuously launching client processes, to ensure that there are at least 10 connected at the same time. The test is considered passed if no run-time errors have occurred and the final summary of statistics produces reasonable values. To run the test 3:
```
$ make test3
```
