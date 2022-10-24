/*
COMANDO PER COMPILARE ED ESEGUIRE IL CODICE:
> g++ parasites_serial.cpp -lallegro -lallegro_primitives
> ./a.out
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>

#define ROWS 700
#define COLS 700
#define STEPS 50
#define SIZE_CELL 1
#define TITLE "Parasites - Emanuele Conforti (220270)"

#define v(r, c) ((r) * COLS + (c)) // per trasformare gli indici di matrice in indici di array


// Stati in cui si può trovare una cella: 
// -EMPTY
// -PARASITE è il predatore
// -GRASS è la preda, ha 3 sotto-stati: SEEDED, GROWING, GROWN
// 
// Transizioni di stato: 
// EMPTY -> SEEDED -> GROWING -> GROWN -> PARASITE -> EMPTY...

enum states {EMPTY = 0, PARASITE, SEEDED_GRASS, GROWING_GRASS, GROWN_GRASS};

unsigned *seed = new unsigned(time(NULL));
// Generatore di numeri casuali
// I numeri casuali serviranno nella funzione di transizione
// per fare in modo che i predatori non mangino tutte le prede o non muoiano.
// In pratica, aiutano a raggiungere un equilibrio tra le due parti, in modo da
// rendere il programma infinito.

int *read_matrix;
int *write_matrix;
int size, GEN = 0;  // Nelle iterazioni con GEN % 2 == 0, faccio sviluppare solo l'erba
                    // mentre nelle iterazioni con GEN % 2 != 0, faccio sviluppare solo i parassiti

// Allegro graphics
ALLEGRO_DISPLAY *display;
ALLEGRO_EVENT event;
ALLEGRO_EVENT_QUEUE *queue;

void init();
void transFunc(int r, int c);
inline void swap();
inline void finalize();

// Allegro graphics
inline int init_allegro();
inline void finalize_allegro();
inline void print();


int main(int argc, char *argv[])
{
    init();
    if(init_allegro() == -1)
        return -1;

    while(true)
    {
        print();
        GEN++;
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                transFunc(r, c);
        swap();
    }

    finalize_allegro();
    finalize();
    return 0;
}

// L'inizializzazione prevede una matrice di GROWN_GRASS e un PARASITE al centro
inline void init()
{
    size = ROWS * COLS;
    read_matrix = new int[size];
    write_matrix = new int[size];

    int mid = ROWS / 2;

    for(int i = 0; i < ROWS; i++) {
        for(int j = 0; j < COLS; j++) {
            if(i == mid && j == mid)
                read_matrix[v(i,j)] = PARASITE;
    
            else read_matrix[v(i,j)] = GROWN_GRASS;
        }
    }
}

inline void print()
{
    al_clear_to_color(al_map_rgb(0, 0, 0));

    for (int i = 0; i < ROWS; ++i)
    {
        for (int j = 0; j < COLS; ++j)
        {
            if (read_matrix[v(i,j)] == PARASITE)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(255,0,0));

            else if (read_matrix[v(i,j)] == SEEDED_GRASS)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(34,139,34));

            else if (read_matrix[v(i,j)] == GROWING_GRASS)  
                al_draw_filled_rectangle(i * SIZE_CELL, j * SIZE_CELL, i * SIZE_CELL + SIZE_CELL, j * SIZE_CELL + SIZE_CELL, al_map_rgb(50,205,50));
            
            else if (read_matrix[v(i,j)] == GROWN_GRASS)  
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


void transFunc(int r, int c)
{   
    int numParasiteCells = 0, numGrassCells = 0, numEmptyCells = 0, randNum;
    bool parasiteFound = false;

    if(GEN % 2 == 0) { // Eseguo la funzione di transizione solo sulle celle GRASS e EMPTY
        switch(read_matrix[v(r,c)]) {
            case GROWN_GRASS:
                write_matrix[v(r,c)] = GROWN_GRASS;
                break;     

            case GROWING_GRASS: // GROWING_GRASS -> GROWN_GRASS
                write_matrix[v(r,c)] = GROWN_GRASS;
                break;

            case SEEDED_GRASS: // SEEDED_GRASS -> GROWING_GRASS
                write_matrix[v(r,c)] = GROWING_GRASS;
                break;

            case PARASITE:
                write_matrix[v(r,c)] = PARASITE;   
                break;

            // ------------------------------------------------------
            // Se una cella EMPTY ha 3 o più vicini GROWN_GRASS,
            // allora diventa GRASS (SEEDED, perchè appena seminato);
            // altrimenti, rimane EMPTY

            case EMPTY:
                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j)
                        if((r+i) < ROWS && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0)
                            if(read_matrix[v(r+i,c+j)] == GROWN_GRASS)
                                numGrassCells++;
                            
                            else if(read_matrix[v(r+i,c+j)] == PARASITE)
                                numParasiteCells++;

                if(numGrassCells >= 3)
                    write_matrix[v(r,c)] = SEEDED_GRASS;
                
                else write_matrix[v(r,c)] = EMPTY;
                break; 
        }
    }

    else { // Eseguo la funzione di transizione solo sui predatori (PARASITE)

        switch(read_matrix[v(r,c)]) {

            // -------------------------------------------------------------------------
            // Se una cella GROWN_GRASS (preda) ha almeno un vicino PARASITE (predatore)
            // e se il numero casuale è compreso tra 1 e 5,
            // allora la cella GROWN_GRASS diventa un PARASITE;
            // altrimenti, rimane GROWN_GRASS

            case GROWN_GRASS:
                for (int i = -1; i <= 1 && !parasiteFound; ++i)
                    for (int j = -1; j <= 1 && !parasiteFound; ++j)

                        if((r+i) < ROWS && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0 && read_matrix[v(r+i,c+j)] == PARASITE)
                            parasiteFound = true;
                
                randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20
                
                if(parasiteFound && randNum <= 5) 
                    write_matrix[v(r,c)] = PARASITE;

                else write_matrix[v(r,c)] = GROWN_GRASS;
                break;

            case GROWING_GRASS:
                write_matrix[v(r,c)] = GROWING_GRASS;
                break;

            case SEEDED_GRASS:
                write_matrix[v(r,c)] = SEEDED_GRASS;
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
                        if((r+i) < ROWS && (r+i) >= 0 && (c+j) < COLS && (c+j) >= 0)
                            if(read_matrix[v(r+i,c+j)] == PARASITE)
                                numParasiteCells++;

                            else if(read_matrix[v(r+i,c+j)] == GROWN_GRASS)
                                numGrassCells++;
                
                randNum = (rand() % 20) + 1; // Numero casuale, compreso tra 1 e 20

                if(numParasiteCells >= 5 || numGrassCells == 0)
                    write_matrix[v(r,c)] = EMPTY;
            
                else if(GEN > 50 && randNum <= 5) 
                    write_matrix[v(r,c)] = EMPTY;

                else write_matrix[v(r,c)] = PARASITE; 
                
                break;

            case EMPTY:
                write_matrix[v(r,c)] = EMPTY;
                break;
        }
    }
}

inline void swap()
{
    int *p = read_matrix;
    read_matrix = write_matrix;
    write_matrix = p;
}

inline void finalize_allegro()
{
    al_destroy_display(display);
    al_destroy_event_queue(queue);
}

inline void finalize()
{
    delete[] read_matrix;
    delete[] write_matrix;
}