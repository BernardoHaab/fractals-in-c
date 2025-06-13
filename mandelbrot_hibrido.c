#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "util.h"
#include <string.h>
#include <omp.h>
#include <bits/getopt_core.h>
#include <omp.h>

#define WIDTH 640
#define HEIGHT 480

#define MESSAGE_CORDS_SIZE 6
#define MAX_ITERATIONS 15000
#define MAX_SIZE 4

#define NUM_FRAMES 30

void controller(int num_frames, int parallel);
void worker(int);

typedef struct
{
  double x_min, x_max, y_min, y_max;
} zoom_coords_t;

void generate_zoom_sequence(zoom_coords_t *coords, int num_frames);
void mandelbrot_render(int *response, zoom_coords_t coords, int width, int height, int frame_id, int parallel);
void saveImage(int *colors, int frame_id);

int main(int argc, char *argv[])
{
  int c;
  int frames = NUM_FRAMES;
  int parallel = 2;
  while ((c = getopt(argc, argv, "f:p:")) != -1)
  {
    if (c == 'f')
    {
      char *endptr;
      frames = strtol(optarg, &endptr, 10);
      printf("Número de frames: %d\n", frames);
    }
    else if (c == 'p')
    {
      char *endptr;
      parallel = strtol(optarg, &endptr, 10);
      printf("Número de processos paralelos (OpenMP): %d\n", parallel);
    }
  }

  int my_rank;

  MPI_Init(&argc, &argv);                  // funcao que inicializa o MPI, todo o código paralelo esta abaixo
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); // pega pega o numero do processo atual (rank)

  double start_time = 0.0, end_time = 0.0;
  if (my_rank == 0)
  {
    start_time = MPI_Wtime();
    controller(frames, parallel);
    end_time = MPI_Wtime();
    printf("Tempo decorrido: %f segundos\n", end_time - start_time);
  }
  else
  {
    worker(parallel);
  }

  MPI_Finalize();
  return EXIT_SUCCESS;
}

void controller(int num_frames, int parallel)
{
  int num_procs;
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
  int num_workers = num_procs - 1;

  zoom_coords_t coords[num_frames];
  generate_zoom_sequence(coords, num_frames);

  int frames_sent = 0;
  int frames_received = 0;
  MPI_Status status;

  for (int i = 1; i < num_procs; i++)
  {
    double message[MESSAGE_CORDS_SIZE] = {
        coords[frames_sent].x_min,
        coords[frames_sent].x_max,
        coords[frames_sent].y_min,
        coords[frames_sent].y_max,
        WIDTH, HEIGHT};
    MPI_Send(message, MESSAGE_CORDS_SIZE, MPI_DOUBLE, frames_sent + 1, frames_sent, MPI_COMM_WORLD);
    frames_sent++;
  }

  int **all_res = malloc(num_frames * sizeof(int *));
  for (int i = 0; i < num_frames; i++)
  {
    all_res[i] = malloc(WIDTH * HEIGHT * sizeof(int));
  }

  while (frames_received < num_frames)
  {
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    int frame_id = status.MPI_TAG;
    MPI_Recv(&all_res[frame_id][0], WIDTH * HEIGHT, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    frames_received++;

    if (frames_sent < num_frames)
    {
      double message[MESSAGE_CORDS_SIZE] = {
          coords[frames_sent].x_min,
          coords[frames_sent].x_max,
          coords[frames_sent].y_min,
          coords[frames_sent].y_max,
          WIDTH, HEIGHT};
      MPI_Send(message, MESSAGE_CORDS_SIZE, MPI_DOUBLE, status.MPI_SOURCE, frames_sent, MPI_COMM_WORLD);
      frames_sent++;
    }
    else
    {
      double fim = -1.0f;
      MPI_Send(&fim, 1, MPI_DOUBLE, status.MPI_SOURCE, 999, MPI_COMM_WORLD);
    }
  }

  printf("Todos os frames recebidos: %d\n", frames_received);
#pragma omp parallel for num_threads(parallel)
  for (int i = 0; i < num_frames; i++)
  {
    saveImage(all_res[i], i);
  }
}

void worker(int parallel)
{
  MPI_Status status;
  printf("Worker started\n");

  while (1)
  {
    double coords[MESSAGE_CORDS_SIZE];
    MPI_Recv(coords, MESSAGE_CORDS_SIZE, MPI_DOUBLE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

    if (status.MPI_TAG == 999)
      break;

    double x_min = coords[0], x_max = coords[1], y_min = coords[2], y_max = coords[3];
    int width = (int)coords[4];
    int height = (int)coords[5];

    int *response = malloc(width * height * sizeof(int));
    zoom_coords_t struct_cords = {x_min, x_max, y_min, y_max};
    mandelbrot_render(response, struct_cords, width, height, status.MPI_TAG, parallel);
    MPI_Send(response, width * height, MPI_INT, 0, status.MPI_TAG, MPI_COMM_WORLD);
    free(response);
  }
}

void generate_zoom_sequence(zoom_coords_t *coords, int num_frames)
{
  double x_center = -1.401155;
  double y_center = 0;
  double scale = 3;
  double final_scale = 0.05;
  double dynamic_zoom_factor = pow(final_scale / scale, 1.0 / (num_frames - 1));

  for (int i = 0; i < num_frames; i++)
  {
    double width = scale;
    double height = scale * HEIGHT / WIDTH;

    coords[i].x_min = x_center - width / 2;
    coords[i].x_max = x_center + width / 2;
    coords[i].y_min = y_center - height / 2;
    coords[i].y_max = y_center + height / 2;

    scale *= dynamic_zoom_factor;
  }
}

void mandelbrot_render(int *response, zoom_coords_t coords, int width, int height, int frame_id, int parallel)
{
  double x_min = coords.x_min;
  double x_max = coords.x_max;
  double y_min = coords.y_min;
  double y_max = coords.y_max;

  double delta_x = (x_max - x_min) / width;
  double delta_y = (y_max - y_min) / height;

  omp_set_num_threads(parallel);
/*
 * For every pixel calculate resulting value until the number becomes too
 * big, or we run out of iterations
 */
#pragma omp parallel for schedule(static) collapse(2) // Paraleliza os loops
  for (int row = 0; row < HEIGHT; row++)
  {
    for (int col = 0; col < WIDTH; col++)
    {
      double y0 = y_max - row * delta_y;

      double x0 = x_min + col * delta_x;
      double x = 0, y = 0;
      int iteration = 0;

      while (x * x + y * y <= MAX_SIZE && iteration < MAX_ITERATIONS)
      {
        double xtemp = x * x - y * y + x0;
        y = 2 * x * y + y0;
        x = xtemp;
        iteration++;
      }
      double norm = (double)iteration / MAX_ITERATIONS;
      double gamma = 0.4f;
      double scaled = powf(norm, gamma);
      int r = (int)(255 * scaled);
      int g = (int)(255 * powf(scaled, 0.7f));
      int b = (int)(255 * powf(scaled, 0.5f));
      int grey = (int)(0.21f * r + 0.72f * g + 0.07f * b);

      response[row * width + col] = grey;
    }
  }
}

void saveImage(int *colors, int frame_id)
{
  char filename[64];
  snprintf(filename, sizeof(filename), "frame_%03d.ppm", frame_id);

  ppm_t *ppm = ppm_create(WIDTH, HEIGHT);

  for (int col = 0; col < WIDTH; col++)
  {
    for (int row = 0; row < HEIGHT; row++)
    {
      int color = colors[row * WIDTH + col];
      ppm_color_t grey = {color, color, color};
      ppm_dot_safe(ppm, col, row, grey);
    }
  }

  FILE *f = fopen(filename, "w");
  ppm_write(ppm, f);
  fclose(f);
  ppm_destroy(ppm);
}
