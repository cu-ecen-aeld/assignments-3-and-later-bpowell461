#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int myWrite(char *name, char *buf) {
    openlog("myWrite", LOG_PID|LOG_CONS, LOG_USER);
    FILE *file = fopen(name, "w");
    syslog(LOG_DEBUG, "Writing to %s to %s\n", buf, name);
    if (file == NULL) {
        syslog(LOG_ERR, "Could not open file \n");
        closelog();
        return 1;
    }
    // Write the string to the file
    if (fprintf(file, "%s", buf) < 0) {
        syslog(LOG_ERR, "Could not write to file \n");
        fclose(file);
        closelog();
        return 1;
    }
    // Close the file
    fclose(file);
    closelog();
    return 0;
}
int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <filename> <string>\n", argv[0]);
        return 1;
    }
    if (myWrite(argv[1], argv[2]) != 0) {
        return 1;
    }
    return 0;
}