#ifndef MOVING_AVERAGE_FILTER_H
#define MOVING_AVERAGE_FILTER_H

// Incluído para usar o tipo 'size_t' (inteiro sem sinal para tamanhos)
#include <cstddef>

/*
 * MovingAverageFilter - Filtro de Média Móvel
 *
 * O filtro de média móvel suaviza um sinal ruidoso tirando a média
 * das últimas N amostras. A cada nova leitura, a amostra mais antiga
 * é descartada e a nova entra no cálculo.
 *
 * Isto é implementado como um buffer circular: o array 'readings'
 * armazena as últimas N amostras, e 'readIndex' aponta sempre para
 * a amostra mais antiga, que será sobrescrita na próxima chamada.
 *
 * Vantagem: muito simples e rápido.
 * Desvantagem: todas as N amostras têm o mesmo peso (diferente de
 * um filtro exponencial, que dá mais peso às amostras recentes).
 */
class MovingAverageFilter {
public:
    /*
     * Construtor: aloca o buffer de leituras no heap (memória dinâmica).
     * Param 'numReadings': quantas amostras entrarão na média (padrão = 5).
     * Ex: com 5 amostras, a cada nova leitura a média é calculada sobre
     * as 5 amostras mais recentes.
     */
    MovingAverageFilter(size_t numReadings = 5);

    /*
     * Destrutor: libera a memória alocada para o buffer.
     * Chamado automaticamente quando o objeto sai de escopo.
     */
    ~MovingAverageFilter();

    /*
     * addReading: adiciona uma nova amostra ao filtro.
     * Remove a mais antiga, insere a nova no lugar, recalcula a média.
     * Retorna o valor médio atualizado.
     */
    float addReading(float value);

    /*
     * reset: zera todas as leituras e o total acumulado.
     * Útil para reiniciar o filtro sem precisar recriar o objeto.
     */
    void reset();

    /*
     * getAverage: retorna a média atual sem modificar o buffer.
     * Útil quando se quer consultar o valor sem inserir nova amostra.
     */
    float getAverage() const;

private:
    float* readings;       // Ponteiro para o array dinâmico de amostras
    size_t numReadings;    // Número de amostras do filtro (tamanho do array)
    size_t readIndex;      // Índice atual no buffer circular
    float total;           // Soma acumulada de todas as amostras no buffer
};

#endif
