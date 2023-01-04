/*
COMANDO PER COMPILARE ED ESEGUIRE IL CODICE:
> mpiCC parasites.cpp -lallegro -lallegro_primitives
> mpirun -np 4 ./a.out
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <iostream>
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <mpi.h>

#define ROWS 500
#define COLS 500
#define STEPS 200
#define SIZE_CELL 3
#define TITLE "Parasites - Emanuele Conforti (220270)"

#define coords(r, c) (r * COLS + (c)) // per trasformare gli indici di matrice in indici di array


// Stati in cui si può trovare una cella: 
// -EMPTY
// -PARASITE è il predatore
// -GRASS è la preda, ha 3 sotto-stati: SEEDED, GROWING, GROWN
// 
// Transizioni di stato: 
// EMPTY -> SEEDED -> GROWING -> GROWN -> PARASITE -> EMPTY...

enum states {EMPTY = 0, PARASITE, SEEDED_GRASS, GROWING_GRASS, GROWN_GRASS};

// Generatore di numeri casuali
// I numeri casuali serviranno nella funzione di transizione
// per fare in modo che i predatori non mangino tutte le prede o non muoiano.
// In pratica, aiutano a raggiungere un equilibrio tra le due parti, in modo da
// rendere il programma infinito.
unsigned *seed = new unsigned(time(NULL));

int *matrix, *localReadMatrix, *localWriteMatrix;
int end = 0, GEN = 0;  // Nelle iterazioni con GEN % 2 == 0, faccio sviluppare solo l'erba
                    // mentre nelle iterazioni con GEN % 2 != 0, faccio sviluppare solo i parassiti

// MPI
MPI_Status status;
MPI_Request req;
MPI_Datatype localMatrixType, borderType;
MPI_Comm comm;
int rank, nthreads, upNeighbor, downNeighbor;

// Allegro graphics
ALLEGRO_DISPLAY *display;
ALLEGRO_EVENT event;
ALLEGRO_EVENT_QUEUE *queue;

void init();
void transFunctionInside();
void transFunctionBorders();
void transFunction(int r, int c);
inline void swap();

// MPI
void MPI_sendBorders();
void MPI_recvBorders();

// Allegro graphics
inline int init_allegro();
inline void finalize_allegro();
inline void print();


int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nthreads);

    MPI_Type_contiguous((ROWS/nthreads)*COLS, MPI_INT, &localMatrixType);
    MPI_Type_commit(&localMatrixType);

    MPI_Type_contiguous(COLS, MPI_INT, &borderType);
    MPI_Type_commit(&borderType);
 
    int dims[1] = {nthreads}; 
    int periods[1] = {1};
    MPI_Cart_create(MPI_COMM_WORLD, 1, dims, periods, 0, &comm);
    MPI_Cart_shift(comm, 0, 1, &upNeighbor, &downNeighbor);

    init();
    if(rank == 0) {
        if(init_allegro() == -1) {
            return -1;
        }
        print();
    }

    MPI_Scatter(matrix, 1, localMatrixType, &localReadMatrix[coords(1,0)], 1, localMatrixType, 0, comm);

    while(!end && GEN < STEPS) {
        MPI_sendBorders();
        
        transFunctionInside();

        MPI_recvBorders();

        transFunctionBorders();

        swap();

        MPI_Gather(&localReadMatrix[coords(1,0)], 1, localMatrixType, matrix, 1, localMatrixType, 0, comm);

        if(rank == 0) {
            print();

            al_peek_next_event(queue, &event);
            if(event.type == ALLEGRO_EVENT_DISPLAY_CLOSE)
                end = 1;
        }
        GEN++;

        MPI_Bcast(&end, 1, MPI_INT, 0, comm);
    }

    if(rank == 0)
        finalize_allegro();

    MPI_Type_free(&localMatrixType);
    MPI_Type_free(&borderType);

    delete [] localReadMatrix;
    delete [] localWriteMatrix;
    delete [] matrix;

    MPI_Finalize();

    return 0;
}

// L'inizializzazione prevede una matrice di GROWN_GRASS e un PARASITE al centro
inline void init()
{   
    int size = (ROWS/nthreads+2)*COLS;
    localReadMatrix = new int[size];
    localWriteMatrix = new int[size];
    matrix = new int[ROWS*COLS];

    int mid = ROWS/2;

    for(int i = 0; i < ROWS; i++) {
        for(int j = 0; j < COLS; j++) {
            if(i == mid && j == mid)
                matrix[coords(i,j)] = PARASITE;
    
            else matrix[coords(i,j)] = GROWN_GRASS;
        }
    }
}

inline void print()
{
    al_clear_to_color(al_map_rgb(0, 0, 0));

    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            if (matrix[coords(i,j)] == PARASITE)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(255,0,0));

            else if (matrix[coords(i,j)] == SEEDED_GRASS)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(34,139,34));

            else if (matrix[coords(i,j)] == GROWING_GRASS)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(50,205,50));
            
            else if (matrix[coords(i,j)] == GROWN_GRASS)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(0,255,0));
            
            else al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(0,0,0));

        }
    }

    al_flip_display();
    al_rest(1.0 / 30.0);
}

inline int init_allegro()
{
    al_init();
    display = al_create_display(COLS * SIZE_CELL, ROWS * SIZE_CELL);
    queue = al_create_event_queue();
    al_init_primitives_addon();
    al_register_event_source(queue, al_get_display_event_source(display));
    al_set_window_title(display, TITLE);

    if(!al_init()) {
        printf("Errore: impossibile inizializzare allegro...\n");
        return -1;
    }
    return 0;
}


void transFunctionInside() 
{
    int start_index = 2, end_index = (ROWS/nthreads)-1;

    for (int r = start_index; r < end_index; ++r)
        for (int c = 0; c < COLS; ++c)
            transFunction(r, c);
}

void transFunctionBorders() 
{
    for(int c = 0; c < COLS; c++) {
        transFunction(1, c);
        transFunction(ROWS/nthreads, c);
    }
}

void transFunction(int r, int c)
{   
    int numParasiteCells = 0, numGrassCells = 0, numEmptyCells = 0, randNum;
    bool parasiteFound = false;

    if(GEN % 2 == 0) { // Eseguo la funzione di transizione solo sulle celle GRASS e EMPTY
        switch(localReadMatrix[coords(r,c)]) {
            case GROWN_GRASS:
                localWriteMatrix[coords(r,c)] = GROWN_GRASS;
                break;     

            case GROWING_GRASS: // GROWING_GRASS -> GROWN_GRASS
                localWriteMatrix[coords(r,c)] = GROWN_GRASS;
                break;

            case SEEDED_GRASS: // SEEDED_GRASS -> GROWING_GRASS
                localWriteMatrix[coords(r,c)] = GROWING_GRASS;
                break;

            case PARASITE:
                localWriteMatrix[coords(r,c)] = PARASITE;   
                break;

            // ------------------------------------------------------
            // Se una cella EMPTY ha 3 o più vicini GROWN_GRASS,
            // allora diventa GRASS (SEEDED, perchè appena seminato);
            // altrimenti, rimane EMPTY

            case EMPTY:
                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j)
                        if((r+i) < (ROWS/nthreads+2) && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0)
                            if(localReadMatrix[coords(r+i,c+j)] == GROWN_GRASS)
                                numGrassCells++;
                            
                            else if(localReadMatrix[coords(r+i,c+j)] == PARASITE)
                                numParasiteCells++;

                if(numGrassCells >= 3)
                    localWriteMatrix[coords(r,c)] = SEEDED_GRASS;
                
                else localWriteMatrix[coords(r,c)] = EMPTY;
                break; 
        }
    }

    else { // Eseguo la funzione di transizione solo sui predatori (PARASITE)

        switch(localReadMatrix[coords(r,c)]) {

            // -------------------------------------------------------------------------
            // Se una cella GROWN_GRASS (preda) ha almeno un vicino PARASITE (predatore)
            // e se il numero casuale è compreso tra 1 e 5,
            // allora la cella GROWN_GRASS diventa un PARASITE;
            // altrimenti, rimane GROWN_GRASS

            case GROWN_GRASS:
                for (int i = -1; i <= 1 && !parasiteFound; ++i)
                    for (int j = -1; j <= 1 && !parasiteFound; ++j)

                        if((r+i) < (ROWS/nthreads+2) && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0 && localReadMatrix[coords(r+i,c+j)] == PARASITE)
                            parasiteFound = true;
                
                randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20
                
                if(parasiteFound && randNum <= 5) 
                    localWriteMatrix[coords(r,c)] = PARASITE;

                else localWriteMatrix[coords(r,c)] = GROWN_GRASS;
                break;

            case GROWING_GRASS:
                localWriteMatrix[coords(r,c)] = GROWING_GRASS;
                break;

            case SEEDED_GRASS:
                localWriteMatrix[coords(r,c)] = SEEDED_GRASS;
                break;
            
            // --------------------------------------------------------------------
            // Se una cella PARASITE ha 5 o più vicini PARASITE (sovrappopolazione)
            // oppure non ha alcun vicino GROWN_GRASS
            // oppure abbiamo superato la 50-esima iterazione (GEN > 50)
            // e il numero casuale è compreso tra 1 e 5,
            // allora muore, cioè diventa EMPTY;
            // altrimenti, rimane PARASITE

            case PARASITE:
                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j)
                        if((r+i) < (ROWS/nthreads+2) && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0)
                            if(localReadMatrix[coords(r+i,c+j)] == PARASITE)
                                numParasiteCells++;

                            else if(localReadMatrix[coords(r+i,c+j)] == GROWN_GRASS)
                                numGrassCells++;
                
                randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20

                if(numParasiteCells >= 5 || numGrassCells == 0)
                    localWriteMatrix[coords(r,c)] = EMPTY;
            
                else if(GEN > 50 && randNum <= 5) 
                    localWriteMatrix[coords(r,c)] = EMPTY;

                else localWriteMatrix[coords(r,c)] = PARASITE; 
                
                break;

            case EMPTY:
                localWriteMatrix[coords(r,c)] = EMPTY;
                break;
        }
    }
}

inline void swap()
{
    int *p = localReadMatrix;
    localReadMatrix = localWriteMatrix;
    localWriteMatrix = p;
}

inline void finalize_allegro()
{
    al_destroy_display(display);
    al_destroy_event_queue(queue);
}

void MPI_sendBorders() 
{
    int sendingUp = 0, sendingDown = 1;

    MPI_Send(&localReadMatrix[coords(1,0)], 1, borderType, upNeighbor, sendingUp, comm, &req);
    MPI_Send(&localReadMatrix[coords((ROWS/nthreads),0)], 1, borderType, downNeighbor, sendingDown, comm, &req);
}

void MPI_recvBorders() 
{
    int recevingFromDown = 0, receivingFromUp = 1;

    MPI_Recv(&localReadMatrix[coords(0,0)], 1, borderType, upNeighbor, receivingFromUp, comm, &status);
    MPI_Recv(&localReadMatrix[coords((ROWS/nthreads+1),0)], 1, borderType, downNeighbor, recevingFromDown, comm, &status);
}