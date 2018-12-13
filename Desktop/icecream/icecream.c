#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include<sys/syscall.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#define NUM_CUSTOMERS 10
#define SECOND 1

void *Cashier(void);
void *Clerk(void);
void *Manager(void*);
void *Customer(void*);
void Init_Semaphores(void);
void Destroy_Semaphores(void);
int RandomInteger();
void MakeCone(void);
bool InspectCone(void);
void Checkout(int linePosition);
void Browse(void);
bool RandomInspection(void);



struct inspection { // struct of globals for Clerk->Manager rendezvous
 sem_t available; // lock used to serialize access to the one Manager
 sem_t requested; // as Manager is asleep, signaled by clerk when cone is ready to inspect
 sem_t finished; // signaled by manager after cone has been inspected,so clerk waits forManager
 bool passed; // status of the last inspection
} inspection;



struct line { // struct of globals for Customer->Cashier line
 sem_t lock; // lock used to serialize access to counter
 int nextPlaceInLine; // counter
 sem_t customers[NUM_CUSTOMERS]; // rendezvous for customer by position
 sem_t customerReady;// signaled by customer when ready to check out
} line;

 sem_t clerksDone;
 




/*
  ONE THREAD PER CUSTOMER
 ONE CLERK THREAD PER CONE
 */


int main()
{

 int i, numCones, totalCones = 0;
 Init_Semaphores();
 
 pthread_t customer_thread[10], cashier_thread, manager_thread;
 for (i = 0; i < NUM_CUSTOMERS; i++) {
 //char name[32];
 //sprintf(name, "Customer %d", i);

 numCones = RandomInteger(4); // each customer wants 1 to 4 cones
 //ThreadNew(name, Customer, 1, numCones);
 pthread_create(&customer_thread[i], NULL, Customer, (void*)numCones);
 totalCones += numCones;
 }

 //ThreadNew("Cashier", Cashier, 0);
 pthread_create(&cashier_thread, NULL, (void *)Cashier, 0);
 

 //ThreadNew("Manager", Manager, 1, totalCones);
 pthread_create(&manager_thread, NULL, Manager, (void*)totalCones);
  
 for (i=0; i<10; i++)
  pthread_join(customer_thread[i], NULL);


int rc = pthread_join(cashier_thread, NULL);
int rm = pthread_join(manager_thread, NULL);
 printf("\n%d\n", rc); printf("\n%d\n", rm);

 printf("All done! :)\n");
 sem_destroy(&clerksDone);
 Destroy_Semaphores();

 return 0;
}





void *Manager(void *arg)
{
 int totalNeeded = (int)arg;
 int numPerfect = 0, numInspections = 0;
 while (numPerfect < totalNeeded) {

 sem_wait(&inspection.requested);
 inspection.passed = InspectCone(); // clerk has lock
 numInspections++;
 if (inspection.passed)
 numPerfect++;
 sem_post(&inspection.finished);
 }
 printf("Inspection success rate %d\n", (100*numPerfect)/numInspections);
pthread_exit(NULL);
}






/*
 * A Clerk thread is dispatched by the customer for each cone they want.
 * The clerk makes the cone and then has to have the manager inspect it.
 * If it doesn't pass, they have to make another. To check with the manager,
 * the clerk has to acquire the exclusive rights to confer with the manager,
 * then signal to wake up the manager, and then wait until they have passed
 * judgment (by writing to a global). We need to be sure that we don't
 * release the inspection lock until we have read the status and are
 * totally done with our inspection. Once we have a perfect ice cream,
 * we signal back to the originating customer by means of the rendezvous
 * semaphore passed as a parameter to this thread.
 */



void *Clerk(void)
{
 
 bool passed = false;
 while (!passed)
 {
  MakeCone();
  sem_wait(&inspection.available);

  sem_post(&inspection.requested);
  sem_wait(&inspection.finished);
  passed = inspection.passed;

  sem_post(&inspection.available);
 }
 sem_post(&clerksDone);
 pthread_exit(NULL);
}






/*
 * The customer dispatches one thread for each cone desired,
 * then browses around while the clerks make the cones. We create
 * our own local generalized rendezvous semaphore that we pass to
 * the clerks so they can notify us as they finish. We use
 * that semaphore to "count" the number of clerks who have finished.
 * In the second loop, we wait once for each clerk, which allows
 * us to efficiently block until all clerks check back in.
 * Then we get in line for the cashier by "taking the next number"
 * that is available and signaling our presence to the cashier.
 * When they call our number (signal the semaphore at our place
 * in line), we're done.
 */




void *Customer(void* arg)
{
 int numConesWanted = (int)arg;
 int i, myPlace;
 
 pthread_t clerk_thread[40]; 

 for (i = 0; i < numConesWanted; i++)
 //ThreadNew("Clerk", Clerk, 1, clerksDone);
 pthread_create(&clerk_thread[i], NULL, (void *)Clerk, 0);

 Browse();
 for(i=0;i<numConesWanted;i++)
  pthread_join(clerk_thread[i], NULL);

 for (i = 0; i < numConesWanted; i++)
 sem_wait(&clerksDone);

 sem_destroy(&clerksDone); 

 sem_wait(&line.lock); //lock used so that Customer gets appropriate/justified number
 myPlace = line.nextPlaceInLine++; 
 sem_post(&line.lock);
 
 sem_post(&line.customerReady); // signal to cashier that they are in line 
 sem_wait(&line.customers[myPlace]); // wait til checked through
 pid_t tid = syscall(SYS_gettid);
 printf("%d done!\n", tid);
 
 pthread_exit(NULL);
}





/*
 * The cashier just checks the customers through, one at a time,
 * as they become ready. In order to ensure that the customers get
 * processed in order, we maintain an array of semaphores that allow us
 * to selectively notify a waiting customer, rather than signaling
 * one combined semaphore without any control over which waiter will
 * get notified.
 */

void *Cashier(void)
{
 int i;
 for (i = 0; i < NUM_CUSTOMERS; i++) {
 sem_wait(&line.customerReady);
 Checkout(i);
 sem_post(&line.customers[i]);
 }
 pthread_exit(NULL);
}





//*********************************************************************



 void Init_Semaphores(void)
{
 int i;
 sem_init(&clerksDone, 0, 0);
 sem_init(&inspection.requested, 0, 0);
 sem_init(&inspection.finished, 0, 0);
 sem_init(&inspection.available, 0, 1);
 inspection.passed = false;
 sem_init(&line.customerReady, 0, 0);
 sem_init(&line.lock, 0, 1);
 line.nextPlaceInLine = 0;
 
 for (i = 0; i < NUM_CUSTOMERS; i++)
 sem_init(&line.customers[i], 0, 0);

}


 void Destroy_Semaphores(void)
{
 
int i;
 sem_destroy(&inspection.requested);
 sem_destroy(&inspection.finished);
 sem_destroy(&inspection.available);
 inspection.passed = false;
 sem_destroy(&line.customerReady);
 sem_destroy(&line.lock);
 
 for (i = 0; i < NUM_CUSTOMERS; i++)
 sem_destroy(&line.customers[i]);

}





void MakeCone()
{
 sleep(SECOND); // sleep random amount
 pid_t tid = syscall(SYS_gettid);
 printf("\t%d making an ice cream cone.\n", tid);
}


bool InspectCone()
{
 bool passed = RandomInspection();
 pid_t tid = syscall(SYS_gettid);
 printf("\t\t%d examining cone, did it pass? %c\n",tid,
 (passed ? 'Y':'N'));
 sleep(SECOND); // sleep random amount
 return passed;
}


void Checkout(int linePosition)
{
 pid_t tid = syscall(SYS_gettid);
 printf("\t\t\t%d checking out customer in line at position #%d.\n",
 tid, linePosition);
 sleep(SECOND); // sleep random amount
}


void Browse(void)
{
 sleep(2*SECOND); // sleep random amount
 pid_t tid = syscall(SYS_gettid);

 printf("%d browsing\n", tid);
 //printf("%s browsing.\n", ThreadName());
}



int RandomInteger()
{
 return (syscall(332) + 1); 
}


bool RandomInspection()
{
 if( rand()%2 == 0)
  return false;
 else 
  return true;
}
