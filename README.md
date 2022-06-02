# Assignment 4: An Audit-Logged Work-Queue based HTTP Server

By: Christopher Jensen

Date: 5/30/2022

## To Run:

This server executes forever, or up until you manually terminate it with CTRL-C. This particular server processes incoming GET PUT and APPEND commands from clients, and returns a valid response.

This program works exactly like the previous HTTP server in assignment 3, where it implements multi-threading with a thread pool and work queue. However, now I have added atomicity and coherency to reading/writing files and the audit log. 

We can execute the server by typing out the following command in the directory where httpserver.c is located:

<b>./httpserver [-t threads] [-l logfile] &lt;port> </b>

Where "threads" is the number of worker threads you want to be handling jobs at the same time, "logfile" is where you want the audit logging to go, and "port" is where we accept connections on a socket that is listening on the port. 
