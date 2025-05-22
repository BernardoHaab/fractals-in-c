#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h> // Include time.h for clock()
#include "util.h"
#include <string.h>

#define WIDTH 640
#define HEIGHT 480

#define MESSAGE_CORDS_SIZE 6
#define MAX_ITERATIONS 1500
#define MAX_SIZE 4

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

void controller();
void worker();

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
  int my_rank; // Identificador deste processo

  MPI_Init(&argc, &argv);                  // funcao que inicializa o MPI, todo o código paralelo esta abaixo
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); // pega pega o numero do processo atual (rank)

  if (my_rank == 0)
  {
    controller();
  }
  else
  {
    worker();
  }

  MPI_Finalize();
  return EXIT_SUCCESS;
}

void controller()
{
  int maxIterations = 150000;
  // int maxIterations = 1500;
  int max_size = 4;

  int proc_n;                             // Numero de processos disparados pelo usuário na linha de comando (np)
  int message;                            // Buffer para as mensagens
  MPI_Status status;                      // estrutura que guarda o estado de retorno
  MPI_Comm_size(MPI_COMM_WORLD, &proc_n); // pega informacao do numero de processos (quantidade total)

  /*
   * dimensions of the complex plane
   */
  float x_min = -2.0;
  float x_max = 1.0;
  float y_min = -1.0;
  float y_max = 1.0;

  int process_por_trabalhador = 1;
  int quant_trabalhadores = proc_n - 1;
  int quant_trabalho = process_por_trabalhador * quant_trabalhadores;

  float saco[quant_trabalho][MESSAGE_CORDS_SIZE];

  float tam_trab = (x_max - x_min) / quant_trabalho;

  int *trabalhador_trabalho = malloc((proc_n - 1) * sizeof(int));
  int width_trabalho = WIDTH / quant_trabalho;
  int height_trabalho = HEIGHT;

  for (int trab = 0; trab < quant_trabalho; trab++)
  {
    saco[trab][0] = x_min + (trab * tam_trab);       // x_min
    saco[trab][1] = x_min + ((trab + 1) * tam_trab); // x_max
    saco[trab][2] = y_min;                           // y_min
    saco[trab][3] = y_max;                           // y_max
    saco[trab][4] = (float)width_trabalho;           // WIDTH do trabalho
    saco[trab][5] = (float)height_trabalho;          // HEIGHT do trabalho
  }

  int curr_trab = 0;

  for (int i = 1; i < proc_n; i++)
  {
    MPI_Send(saco[curr_trab], MESSAGE_CORDS_SIZE, MPI_FLOAT, i, 1, MPI_COMM_WORLD);
    trabalhador_trabalho[i - 1] = curr_trab;
    curr_trab++;
  }

  int *image = malloc(WIDTH * HEIGHT * sizeof(int));
  int quant_responses = 0;

  while (quant_responses < quant_trabalho)
  {
    int pixels_por_trabalho = width_trabalho * height_trabalho;
    int res_size = pixels_por_trabalho * sizeof(int);
    int *res = malloc(res_size);
    MPI_Recv(res, pixels_por_trabalho, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
    quant_responses++;
    int trabalhador = status.MPI_SOURCE;
    int idx_trabalho = trabalhador_trabalho[trabalhador - 1];

    // aloca a resposta na posição correta da matriz da imagem
    // matriz da imagem esta em representação linear
    for (int row = 0; row < height_trabalho; row++)
    {
      memcpy(&image[row * WIDTH + idx_trabalho * width_trabalho], sliceIntArray(res, row * width_trabalho, (row + 1) * width_trabalho, NULL), width_trabalho * sizeof(int));
    }
    free(res); // Libere a memória de res após copiar

    if (curr_trab < quant_trabalho)
    {
      // Envia novo trabalho
      MPI_Send(saco[curr_trab], MESSAGE_CORDS_SIZE, MPI_FLOAT, trabalhador, 1, MPI_COMM_WORLD);
      trabalhador_trabalho[trabalhador - 1] = curr_trab;
      curr_trab++;
    }
    else
    {
      int fim = -1.;
      MPI_Send(&fim, MESSAGE_CORDS_SIZE, MPI_FLOAT, trabalhador, 100, MPI_COMM_WORLD);
    }
  }

  ppm_t *ppm = ppm_create(WIDTH, HEIGHT);

  for (int col = 0; col < WIDTH; col++)
  {
    for (int row = 0; row < HEIGHT; row++)
    {
      int color = image[row * WIDTH + col];

      ppm_color_t grey = {color, color, color};
      ppm_dot_safe(ppm, col, row, grey);
    }
  }

  FILE *f = fopen("outMpi.ppm", "w");
  ppm_write(ppm, f);
  fclose(f);

  ppm_destroy(ppm);
}

void worker()
{
  float coords[MESSAGE_CORDS_SIZE];
  MPI_Status status; // estrutura que guarda o estado de retorno

  MPI_Recv(coords, MESSAGE_CORDS_SIZE, MPI_FLOAT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

  if (status.MPI_TAG == 100) // Se é mensagem de fim termina trabalhador
    return;

  /*
   * dimensions of the complex plane
   */
  float x_min = coords[0];
  float x_max = coords[1];
  float y_min = coords[2];
  float y_max = coords[3];
  int width = (int)coords[4];
  int height = (int)coords[5];
  int pixels_por_trabalho = width * height;

  /*
   * For each row and each column set real and imag parts of the complex
   * number to be used in iteration
   */
  float delta_x = (x_max - x_min) / width;
  float delta_y = (y_max - y_min) / height;
  float *Q = malloc(height * sizeof(float));
  float *P = malloc(width * sizeof(float));
  Q[0] = y_max;
  P[0] = x_min;
  for (int row = 1; row < height; row++)
    Q[row] = Q[row - 1] - delta_y;
  for (int col = 1; col < width; col++)
    P[col] = P[col - 1] + delta_x;

  /**
   * Matriz de resposta representada em um array - espaço alocado continuamente
   * Acesso: response[row * width + col]
   */
  int *response = malloc(width * height * sizeof(int));

  for (int i = 0; i < width * height; i++)
  {
    response[i] = -100;
  }

  /*
   * For every pixel calculate resulting value until the number becomes too
   * big, or we run out of iterations
   */
  for (int col = 0; col < width; col++)
  {
    for (int row = 0; row < height; row++)
    {
      float x_square = 0.0;
      float y_square = 0.0;
      float x = 0.0;
      float y = 0.0;

      int color = 1;
      while (color < MAX_ITERATIONS && x_square + y_square < MAX_SIZE)
      {
        x_square = x * x;
        y_square = y * y;
        y = 2 * x * y + Q[row];
        x = x_square - y_square + P[col];
        color++;
      }
      response[row * width + col] = (color / MAX_ITERATIONS) * 255;
    }
  }

  MPI_Send(response, pixels_por_trabalho, MPI_INT, 0, 1, MPI_COMM_WORLD);
}
