#include "MovingAverageFilter.h"

/*
 * Construtor
 *
 * Aloca um array de 'numReadings' floats no heap.
 * O '()' após o new faz com que todos os elementos sejam
 * inicializados com zero (value-initialization).
 *
 * Exemplo: se numReadings = 3, o array será [0.0, 0.0, 0.0]
 * e total = 0.0. A primeira média será (0+0+0)/3 = 0.
 */
MovingAverageFilter::MovingAverageFilter(size_t numReadings)
    : readings(new float[numReadings]()),
      numReadings(numReadings),
      readIndex(0),
      total(0.0f) {}

/*
 * Destrutor
 *
 * Libera a memória alocada no construtor.
 * Importante para evitar vazamento de memória (memory leak).
 */
MovingAverageFilter::~MovingAverageFilter() {
    delete[] readings;
}

/*
 * addReading
 *
 * Adiciona uma nova amostra ao buffer circular e retorna a média atualizada.
 *
 * Funcionamento:
 * 1. Subtrai do 'total' a amostra mais antiga (readings[readIndex]).
 * 2. Guarda a nova amostra na mesma posição (sobrescrevendo a antiga).
 * 3. Soma a nova amostra ao 'total'.
 * 4. Avança o readIndex circularmente com o operador %.
 * 5. Retorna total / numReadings (a média aritmética simples).
 *
 * Exemplo com 3 amostras:
 *   readings = [10, 20, 30], readIndex = 0, total = 60
 *   addReading(40):
 *     total = 60 - 10 = 50
 *     readings[0] = 40
 *     total = 50 + 40 = 90
 *     readIndex = 1
 *     retorna 90/3 = 30  <- média de [40, 20, 30]
 *
 * Próxima chamada: readings = [40, 20, 30], readIndex = 1
 *   addReading(50):
 *     total = 90 - 20 = 70
 *     readings[1] = 50
 *     total = 70 + 50 = 120
 *     readIndex = 2
 *     retorna 120/3 = 40  <- média de [40, 50, 30]
 */
float MovingAverageFilter::addReading(float value) {
    total = total - readings[readIndex];
    readings[readIndex] = value;
    total = total + readings[readIndex];
    readIndex = (readIndex + 1) % numReadings;
    return total / numReadings;
}

/*
 * reset
 *
 * Zera todas as amostras, o total e volta ao índice inicial.
 * Após reset, o filtro começa de novo como se tivesse acabado de ser criado.
 */
void MovingAverageFilter::reset() {
    total = 0.0f;
    readIndex = 0;
    for (size_t i = 0; i < numReadings; i++) {
        readings[i] = 0.0f;
    }
}

/*
 * getAverage
 *
 * Retorna a média atual sem modificar o buffer.
 * Método 'const' porque não altera o estado do objeto.
 * Útil para debug ou para exibir o valor sem inserir nova leitura.
 */
float MovingAverageFilter::getAverage() const {
    return total / numReadings;
}
