
// Compile and run:
//  > mpicc parasites.cpp -lallegro -lallegro_primitives
//  > mpirun -np 4 ./a.out


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include "mpi.h"

#define STEPS 1000
#define SIZE_CELL 4
#define TITLE "Parasites - Emanuele Conforti (220270)"

inline void init();
inline void finalize();
inline void transFunction(int row, int col);
inline void transFunctionBorders();
inline void transFunctionInside();
inline int coords(int r, int c);

// Allegro graphics
inline int init_allegro();
inline void finalize_allegro();
inline void print();

// MPI
inline void MPI_sendBorders();
inline void MPI_recvBorders();
inline void swap();

// Stati in cui si può trovare una cella: 
// -EMPTY
// -PARASITE è il predatore
// -GRASS è la preda, ha 3 sotto-stati: SEEDED, GROWING, GROWN
// 
// Transizioni di stato: 
// EMPTY -> SEEDED -> GROWING -> GROWN -> PARASITE -> EMPTY...

enum states {EMPTY = 0, PARASITE, SEEDED_GRASS, GROWING_GRASS, GROWN_GRASS};

int ROWS = 300, COLS, end = 0, *localReadMatrix, *localWriteMatrix, *matrix, GEN = 0;

// Nelle iterazioni con GEN % 2 == 0, faccio sviluppare solo l'erba
// mentre nelle iterazioni con GEN % 2 != 0, faccio sviluppare solo i parassiti


// Allegro graphics
ALLEGRO_DISPLAY *display;
ALLEGRO_EVENT event;
ALLEGRO_EVENT_QUEUE *queue;

// MPI
MPI_Datatype borderType;
MPI_Datatype localMatrixType;
MPI_Comm comm;
int rank, nthreads, upNeighbor, downNeighbor;
// double start_time, end_time;

int main(int argc, char** argv){

    MPI_Init(&argc, &argv);
    
    // if(rank == 0)
    //     start_time = MPI_Wtime();

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nthreads);

    while(ROWS%nthreads!=0) {ROWS--;}
    COLS = ROWS;

    if(rank == 0)
        printf("ROWS: %d --- COLS: %d\n", ROWS, COLS);
    
    // Generatore di numeri casuali
    // I numeri casuali serviranno nella funzione di transizione
    // per fare in modo che i predatori non mangino tutte le prede o non muoiano.
    // In pratica, aiutano a raggiungere un equilibrio tra le due parti, in modo da
    // rendere il programma infinito.
    srand((unsigned)time(NULL) + rank);

    localReadMatrix = (int*) calloc((ROWS/nthreads+2)*COLS,sizeof(int));
    localWriteMatrix = (int*) calloc((ROWS/nthreads+2)*COLS,sizeof(int));

    int dimensions[1] = {nthreads};
    int periods[1] = {0};
    
    MPI_Cart_create(MPI_COMM_WORLD, 1, dimensions, periods, 0, &comm);
    MPI_Cart_shift(comm, 0, 1, &upNeighbor, &downNeighbor);

    MPI_Type_contiguous(COLS, MPI_INT, &borderType);
    MPI_Type_contiguous((ROWS/nthreads)*COLS, MPI_INT, &localMatrixType);
    MPI_Type_commit(&borderType);
    MPI_Type_commit(&localMatrixType);

    if(rank == 0){
        matrix = (int*) calloc(ROWS*COLS, sizeof(int));
        if(init_allegro() == -1)
            MPI_Abort(comm, -1);
    }

    init();

    while(!end && GEN < STEPS){

        MPI_sendBorders();       // Invio ASINCRONO dei bordi: ogni processo invia i bordi, 

        transFunctionInside();   // poi esegue la funzione di transizione sulle celle interne,

        MPI_recvBorders();       // riceve i bordi dai processi vicini

        transFunctionBorders();  // e applica la funzione di transizione alle celle rimanenti 
                                 // (sfruttando i bordi appena ricevuti)
        swap();     

        // Ogni processo invia la sua sotto-matrice locale al processo con rank 0, che si occuperà della stampa
        MPI_Gather(&localReadMatrix[coords(1,0)], 1, localMatrixType, matrix, 1, localMatrixType, 0, comm);
        
        if(rank == 0){
            print();
            al_peek_next_event(queue, &event);
            if(event.type == ALLEGRO_EVENT_DISPLAY_CLOSE)
                end = 1; 
            GEN++;                        
        }

        MPI_Bcast(&GEN, 1, MPI_INT, 0, comm);
        MPI_Bcast(&end, 1, MPI_INT, 0, comm);
    }

    if(rank == 0) {
        // end_time = MPI_Wtime();
        printf("ROWS: %d --- COLS: %d\n", ROWS, COLS);
        printf("STEPS: %d\n", GEN);
        // printf("%d thread --- Time: %lf\n", nthreads, (end_time - start_time)*1000);
    }    

    finalize();

    MPI_Finalize();

    return 0;
}

// L'inizializzazione prevede una matrice di GROWN_GRASS e un PARASITE al centro
inline void init()
{
    for(int i = 1; i <= (ROWS/nthreads); i++) {
        for(int j = 0; j < COLS; j++) {
            if(rank == (nthreads/2) && i == 1 && j == (COLS/2)) {
                localReadMatrix[coords(i,j)] = PARASITE;
            }
            else localReadMatrix[coords(i,j)] = GROWN_GRASS;
        }
    }
}

int coords(int r, int c) { return (r*COLS+c); }  // per trasformare gli indici di matrice in indici di array


int init_allegro(){
    if(!al_init()){
        printf("Error: failed to initalize allegro!\n");
        return -1;
    }

    display = al_create_display(COLS*SIZE_CELL, ROWS*SIZE_CELL);
    queue = al_create_event_queue();

    al_init_primitives_addon();

    al_register_event_source(queue, al_get_display_event_source(display));
	al_set_window_title(display, TITLE);
    return 0;
}

void print(){
    al_clear_to_color(al_map_rgb(0, 0, 0));
    for(int y = 0; y < ROWS; ++y){
        for(int x = 0; x < COLS; ++x){
            switch (matrix[coords(y,x)])
            {
                case EMPTY:
                    al_draw_filled_rectangle(x * (SIZE_CELL), y * (SIZE_CELL), x * (SIZE_CELL) + SIZE_CELL, y * (SIZE_CELL) + SIZE_CELL, al_map_rgb(0,0,0));  
                    break;
                
                case SEEDED_GRASS: 
                    al_draw_filled_rectangle(x * (SIZE_CELL), y * (SIZE_CELL), x * (SIZE_CELL) + SIZE_CELL, y * (SIZE_CELL) + SIZE_CELL, al_map_rgb(34,139,34));   
                    break;
                
                case GROWING_GRASS: 
                    al_draw_filled_rectangle(x * (SIZE_CELL), y * (SIZE_CELL), x * (SIZE_CELL) + SIZE_CELL, y * (SIZE_CELL) + SIZE_CELL, al_map_rgb(50,205,50));
                    break;
                
                case GROWN_GRASS: 
                    al_draw_filled_rectangle(x * (SIZE_CELL), y * (SIZE_CELL), x * (SIZE_CELL) + SIZE_CELL, y * (SIZE_CELL) + SIZE_CELL, al_map_rgb(0,255,0));  
                    break;
                
                case PARASITE: 
                    al_draw_filled_rectangle(x * (SIZE_CELL), y * (SIZE_CELL), x * (SIZE_CELL) + SIZE_CELL, y * (SIZE_CELL) + SIZE_CELL, al_map_rgb(255,0,0));  
                    break;

                default: break;
            }
        }
    }
    al_flip_display();
    al_rest(1.0 / 60.0);
}

void finalize_allegro(){

    al_destroy_event_queue(queue);
    al_destroy_display(display);
    queue = 0;
    display = 0;
}

// Viene eseguita la funzione di transizione sulle celle che non hanno bordi come vicini
void transFunctionInside(){
    int start_index = 2, end_index = (ROWS/nthreads)-1;

    if(rank == 0)
        start_index--;
    if(rank == (nthreads-1))
        end_index++;

    for(int i = start_index; i <= end_index; ++i)
        for(int j = 0; j < COLS; ++j)
            transFunction(i, j);
}

// Viene eseguita la funzione di transizione sulle celle aventi i bordi come vicini,
// dopo aver ricevuti i bordi dagli altri processi 
void transFunctionBorders(){

    int start_index = 1, end_index = (ROWS/nthreads);

    for(int j = 0; j < COLS; ++j){
        if(rank != 0)
            transFunction(1, j);

        if(rank != (nthreads-1))
            transFunction(ROWS/nthreads, j);
        }      
}

void transFunction(int r, int c){
    int numParasiteCells = 0, numGrassCells = 0, numEmptyCells = 0, randNum;
    bool parasiteFound = false;

    if(GEN % 2 == 0) { // Eseguo la funzione di transizione solo sulle celle GRASS e EMPTY
        if(localReadMatrix[coords(r,c)] == GROWN_GRASS)
            localWriteMatrix[coords(r,c)] = GROWN_GRASS;

        else if(localReadMatrix[coords(r,c)] == GROWING_GRASS) // GROWING_GRASS -> GROWN_GRASS
            localWriteMatrix[coords(r,c)] = GROWN_GRASS;

        else if(localReadMatrix[coords(r,c)] == SEEDED_GRASS) // SEEDED_GRASS -> GROWING_GRASS
            localWriteMatrix[coords(r,c)] = GROWING_GRASS;

        else if(localReadMatrix[coords(r,c)] == PARASITE)
            localWriteMatrix[coords(r,c)] = PARASITE;   

            // ------------------------------------------------------
            // Se una cella EMPTY ha 3 o più vicini GROWN_GRASS,
            // allora diventa GRASS (SEEDED, perchè appena seminato);
            // altrimenti, rimane EMPTY

        else {
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j)
                    if(!((r+i) == 0 && rank == 0) && !((r+i) == (ROWS/nthreads+1) && rank == (nthreads-1)) && (c+j) < COLS && (c+j) >= 0)
                        if(localReadMatrix[coords(r+i,c+j)] == GROWN_GRASS)
                            numGrassCells++;

            if(numGrassCells >= 3)
                localWriteMatrix[coords(r,c)] = SEEDED_GRASS;
                
            else localWriteMatrix[coords(r,c)] = EMPTY;
        }
    }

    else { // Eseguo la funzione di transizione solo sui predatori (PARASITE)

            // -------------------------------------------------------------------------
            // Se una cella GROWN_GRASS (preda) ha almeno un vicino PARASITE (predatore)
            // e se il numero casuale è compreso tra 1 e 5,
            // allora la cella GROWN_GRASS diventa un PARASITE;
            // altrimenti, rimane GROWN_GRASS

        if(localReadMatrix[coords(r,c)] == GROWN_GRASS) {
            for (int i = -1; i <= 1; ++i) {
                for (int j = -1; j <= 1; ++j) {
                    if(!((r+i) == 0 && rank == 0) && !((r+i) == ((ROWS/nthreads)+1) && rank == (nthreads-1)) && (c+j) < COLS && (c+j) >= 0 && localReadMatrix[coords(r+i,c+j)] == PARASITE) { 
                        parasiteFound = true;
                        randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20
                    }
                }
            }
                
            if(parasiteFound && randNum <= 5) 
                localWriteMatrix[coords(r,c)] = PARASITE;

            else localWriteMatrix[coords(r,c)] = GROWN_GRASS;
        }

        else if(localReadMatrix[coords(r,c)] == GROWING_GRASS)
            localWriteMatrix[coords(r,c)] = GROWING_GRASS;

        else if(localReadMatrix[coords(r,c)] == SEEDED_GRASS)
            localWriteMatrix[coords(r,c)] = SEEDED_GRASS;
            
            // --------------------------------------------------------------------
            // Se una cella PARASITE ha 5 o più vicini PARASITE (sovrappopolazione)
            // oppure non ha alcun vicino GROWN_GRASS
            // oppure abbiamo superato la 50-esima iterazione (GEN > 50)
            // e il numero casuale è compreso tra 1 e 5,
            // allora muore, cioè diventa EMPTY;
            // altrimenti, rimane PARASITE

        else if(localReadMatrix[coords(r,c)] == PARASITE) {
            for (int i = -1; i <= 1; ++i) {
                for (int j = -1; j <= 1; ++j) {
                    if(!((r+i) == 0 && rank == 0) && !((r+i) == (ROWS/nthreads+1) && rank == (nthreads-1)) && (c+j) < COLS && (c+j) >= 0) {
                        if(localReadMatrix[coords(r+i,c+j)] == PARASITE)
                            numParasiteCells++;

                        else if(localReadMatrix[coords(r+i,c+j)] == GROWN_GRASS)
                            numGrassCells++;
                        }
                    }
                }
                
            randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20

            if(numParasiteCells >= 5 || numGrassCells == 0)
                localWriteMatrix[coords(r,c)] = EMPTY;
            
            else if(GEN > 50 && randNum <= 5) 
                localWriteMatrix[coords(r,c)] = EMPTY;

            else localWriteMatrix[coords(r,c)] = PARASITE;      
        }

        else localWriteMatrix[coords(r,c)] = EMPTY;
    }   
}

// invio bordi NON BLOCCANTE (asincrono)
void MPI_sendBorders(){
    
    MPI_Request req;

    int sendingUp = 0, sendingDown = 1;

    // Il processo con rank 0 invia solo al suo vicino sud 
    if(rank == 0)
        MPI_Isend(localReadMatrix+coords((ROWS/nthreads),0), 1, borderType, downNeighbor, sendingDown, comm, &req);

    // Il processo con rank nthreads-1 invia solo al suo vicino nord
    else if(rank == nthreads-1)
        MPI_Isend(localReadMatrix+coords(1,0), 1, borderType, upNeighbor, sendingUp, comm, &req);

    else{
        MPI_Isend(localReadMatrix+coords(1,0), 1, borderType, upNeighbor, sendingUp, comm, &req);
        MPI_Isend(localReadMatrix+coords((ROWS/nthreads),0), 1, borderType, downNeighbor, sendingDown, comm, &req);
    }
}

void MPI_recvBorders(){
    
    MPI_Status status;

    int receivingFromDown = 0, receivingFromUp = 1;

    // Il processo con rank 0 riceve solo dal suo vicino sud 
    if(rank == 0)
        MPI_Recv(localReadMatrix+coords((ROWS/nthreads)+1,0), 1, borderType, downNeighbor, receivingFromDown, comm, &status);

    // Il processo con rank nthreads-1 riceve solo dal suo vicino nord 
    else if(rank == nthreads-1)
        MPI_Recv(localReadMatrix+coords(0,0), 1, borderType, upNeighbor, receivingFromUp, comm, &status);

    else{
        MPI_Recv(localReadMatrix+coords(0,0), 1, borderType, upNeighbor, receivingFromUp, comm, &status);
        MPI_Recv(localReadMatrix+coords((ROWS/nthreads)+1,0), 1, borderType, downNeighbor, receivingFromDown, comm, &status);
    }
}

void swap(){
    free(localReadMatrix);
    localReadMatrix = localWriteMatrix;
    localWriteMatrix = (int*) calloc((ROWS/nthreads+2)*COLS,sizeof(int));
}


void finalize(){
    if(rank == 0){
        finalize_allegro();
        free(matrix);
        matrix = 0;
    }
    
    free(localWriteMatrix);
    free(localReadMatrix);
    localReadMatrix = localWriteMatrix = 0;
}