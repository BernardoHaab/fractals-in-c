#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <stdio.h>

#include "util.h"

#define WIDTH 640
#define HEIGHT 480

const ppm_color_t colors[] = {
    PPM_BLACK,
    PPM_BLUE,
    PPM_GREEN,
    PPM_CYAN,
    PPM_RED,
    PPM_MAGENTA,
    PPM_BROWN,
    PPM_LIGHT_GREY,
    PPM_DARK_GREY,
    PPM_BRIGHT_BLUE,
    PPM_BRIGHT_GREEN,
    PPM_BRIGHT_CYAN,
    PPM_BRIGHT_RED,
    PPM_BRIGHT_MAGENTA,
    PPM_YELLOW,
    PPM_WHITE
};
const int colornum = sizeof(colors) / sizeof(colors[0]);

#define TAG_TASK 1
#define TAG_RESULT 2
#define TAG_STOP 3

int main(int argc, char *argv[])
{
    int my_rank, proc_n;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &proc_n);

    int max_iterations = 512;
    int max_size = 4;
    char *output_filename = "output.ppm";

    int c;
    while ((c = getopt(argc, argv, "o:i:s:")) != -1)
    {
        char *endptr;
        switch (c)
        {
        case 'o':
            output_filename = optarg;
            break;
        case 'i':
            max_iterations = strtol(optarg, &endptr, 10);
            break;
        case 's':
            max_size = strtol(optarg, &endptr, 10);
            break;
        default:
            MPI_Finalize();
            return EXIT_FAILURE;
        }
    }

    // Complex plane boundaries
    float x_min = -2.0;
    float x_max = 1.0;
    float y_min = -1.0;
    float y_max = 1.0;

    float delta_x = (x_max - x_min) / WIDTH;
    float delta_y = (y_max - y_min) / HEIGHT;

    float Q[HEIGHT];
    float P[WIDTH];
    Q[0] = y_max;
    for (int row = 1; row < HEIGHT; row++)
        Q[row] = Q[row - 1] - delta_y;
    P[0] = x_min;
    for (int col = 1; col < WIDTH; col++)
        P[col] = P[col - 1] + delta_x;

    if (my_rank == 0)
    {
        // Master process
        ppm_t *ppm = ppm_create(WIDTH, HEIGHT);

        int next_row = 0;
        int active_slaves = proc_n - 1;
        MPI_Status status;

        // Start timer
        double start_time = MPI_Wtime();

        // Send initial tasks to slaves
        for (int slave = 1; slave < proc_n && next_row < HEIGHT; slave++)
        {
            MPI_Send(&next_row, 1, MPI_INT, slave, TAG_TASK, MPI_COMM_WORLD);
            next_row++;
        }

        while (active_slaves > 0)
        {
            // Buffer to receive one row of colors (WIDTH pixels)
            ppm_color_t row_colors[WIDTH];

            // Receive computed row from any slave
            int row_index;
            MPI_Recv(&row_index, 1, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
            int slave = status.MPI_SOURCE;

            MPI_Recv(row_colors, WIDTH, MPI_INT, slave, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Store the row in ppm image
            for (int col = 0; col < WIDTH; col++)
            {
                ppm_dot_safe(ppm, col, row_index, row_colors[col]);
            }

            // Send next task or stop signal
            if (next_row < HEIGHT)
            {
                MPI_Send(&next_row, 1, MPI_INT, slave, TAG_TASK, MPI_COMM_WORLD);
                next_row++;
            }
            else
            {
                // No more tasks, send stop signal
                MPI_Send(NULL, 0, MPI_INT, slave, TAG_STOP, MPI_COMM_WORLD);
                active_slaves--;
            }
        }

        // Write output file
        FILE *f = fopen(output_filename, "w");
        ppm_write(ppm, f);
        fclose(f);
        ppm_destroy(ppm);

        double end_time = MPI_Wtime();
        printf("Time elapsed: %.2f seconds\n", end_time - start_time);
    }
    else
    {
        // Slave process
        MPI_Status status;
        while (1)
        {
            int row_index;
            MPI_Recv(&row_index, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

            if (status.MPI_TAG == TAG_STOP)
            {
                // No more work
                break;
            }

            // Compute the row
            ppm_color_t row_colors[WIDTH];

            for (int col = 0; col < WIDTH; col++)
            {
                float x_square = 0.0;
                float y_square = 0.0;
                float x = 0.0;
                float y = 0.0;

                int color = 1;
                while (color < max_iterations && x_square + y_square < max_size)
                {
                    x_square = x * x;
                    y_square = y * y;
                    y = 2 * x * y + Q[row_index];
                    x = x_square - y_square + P[col];
                    color++;
                }
                row_colors[col] = colors[color % colornum];
            }

            // Send back the row index and computed colors
            MPI_Send(&row_index, 1, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            MPI_Send(row_colors, WIDTH, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        }
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}