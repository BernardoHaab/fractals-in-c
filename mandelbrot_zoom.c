#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h> // Include time.h for clock()
#include "util.h"
#include <string.h>
#include <omp.h>
#include <bits/getopt_core.h>

#define WIDTH 640
#define HEIGHT 480

#define MESSAGE_CORDS_SIZE 6
#define MAX_ITERATIONS 15000
#define MAX_SIZE 4

#define NUM_FRAMES 30

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
    PPM_WHITE};
const int colornum = sizeof(colors) / sizeof(colors[0]);

void controller(int num_frames);
void worker();

typedef struct
{
  double x_min, x_max, y_min, y_max;
} zoom_coords_t;

void generate_zoom_sequence(zoom_coords_t *coords, int num_frames)
{
  double x_center = -1.401155;
  double y_center = 0;
  double scale = 3; // zoom inicial

  double final_scale = 0.05; // Defina o valor desejado para o zoom final
  double dynamic_zoom_factor = pow(final_scale / scale, 1.0 / (num_frames - 1));
  for (int i = 0; i < num_frames; i++)
  {
    double width = scale;
    double height = scale * HEIGHT / WIDTH;

    coords[i].x_min = x_center - width / 2;
    coords[i].x_max = x_center + width / 2;
    coords[i].y_min = y_center - height / 2;
    coords[i].y_max = y_center + height / 2;

    // Ajusta o ZOOM_FACTOR dinamicamente para que o zoom final seja o mesmo independentemente de num_frames
    scale *= dynamic_zoom_factor;
  }
}

void mandelbrot_render(int *response, double x_min, double x_max, double y_min, double y_max, int width, int height, int frame_id)
{
  double delta_x = (x_max - x_min) / width;
  double delta_y = (y_max - y_min) / height;

  for (int row = 0; row < height; row++)
  {
    double y0 = y_max - row * delta_y;
    for (int col = 0; col < width; col++)
    {
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

int *sliceIntArray(int *source, int from, int to, int *target)
{
  // Invalid, return null.

  if (to <= from)
  {
    return NULL;
  }

  // Only allocate if target buffer not given by caller.

  if (target == NULL)
  {
    target = malloc((to - from) * sizeof(int));
    if (target == NULL)
    {
      return NULL;
    }
  }

  // Copy the data and return it.

  memcpy(target, &(source[from]), (to - from) * sizeof(int));

  return target;
}

int main(int argc, char *argv[])
{
  int c;
  int frames = 0;
  while ((c = getopt(argc, argv, "f:")) != -1)
  {
    if (c == 'f')
    {
      char *endptr;
      frames = strtol(optarg, &endptr, 10);
      printf("Número de frames: %d\n", frames);
    }
    if (frames <= 0)
    {
      fprintf(stderr, "Número de frames inválido. Usando o padrão de %d frames.\n", NUM_FRAMES);
      frames = NUM_FRAMES;
    }
  }

  int my_rank; // Identificador deste processo

  MPI_Init(&argc, &argv);                  // funcao que inicializa o MPI, todo o código paralelo esta abaixo
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); // pega pega o numero do processo atual (rank)

  double start_time = 0.0, end_time = 0.0;
  if (my_rank == 0)
  {
    start_time = MPI_Wtime();
    controller(frames);
    end_time = MPI_Wtime();
    printf("Tempo decorrido: %f segundos\n", end_time - start_time);
  }
  else
  {
    worker();
  }

  MPI_Finalize();
  return EXIT_SUCCESS;
}

void saveImage(int *colors, int frame_id)
{
  char filename[64];
  snprintf(filename, sizeof(filename), "frame_%03d.ppm", frame_id);
  // Save image
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

void controller(int num_frames)
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
    printf("Enviando frame %d para trabalhador %d\n", frames_sent, frames_sent + 1);
    frames_sent++;
  }

  while (frames_received < num_frames)
  {
    int *res = malloc(WIDTH * HEIGHT * sizeof(int));
    MPI_Recv(res, WIDTH * HEIGHT, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    int frame_id = status.MPI_TAG;
    saveImage(res, frame_id);
    free(res);
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
}

void worker()
{
  MPI_Status status;

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
    mandelbrot_render(response, x_min, x_max, y_min, y_max, width, height, status.MPI_TAG);
    MPI_Send(response, width * height, MPI_INT, 0, status.MPI_TAG, MPI_COMM_WORLD);
    free(response);
  }
}
