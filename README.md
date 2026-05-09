# Cache-Oblivious Static Tree

## Integrantes

- Jhon Chilo Gonzales
- Sergio Delgado Amado

## Descripcion breve

Este proyecto implementa un **cache-oblivious static tree** para busqueda de enteros de 32 bits. El arbol se construye como un BST balanceado y se almacena en un arreglo usando un layout tipo **van Emde Boas**, con el objetivo de mejorar la localidad de cache sin depender de un tamano fijo de bloque.

Para mostrar la eficiencia, el programa compara el tiempo de busqueda contra un **BST con punteros**. Ambos arboles se construyen con exactamente los mismos elementos y reciben las mismas consultas aleatorias.

## Requisitos cubiertos

- Cache-oblivious static tree implementado en `include/cache_oblivious_static_tree.h` y `src/cache_oblivious_static_tree.cpp`.
- Comparacion contra BST con punteros en `main.cpp`.
- Construccion con los mismos elementos en ambos arboles.
- Parametro de elementos: por defecto `1,000,000`.
- Parametro de consultas: por defecto `1,000,000`.
- Parametro `B` como block size target: por defecto `64` bytes.
- Promedio de `T` experimentos: por defecto `5`.
- Claves y consultas usando `std::int32_t`.
- Resultados guardados en CSV.
- Hardware descrito en `hardware.txt`.
- Tests intensivos en `tests/cache_tree_tests.cpp`.

## Compilacion

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Ejecucion del experimento

```bash
./build/cpp_qt_learn --n 1000000 --queries 1000000 --trials 5 --block-size 64
```

Tambien se puede ejecutar una version rapida:

```bash
./build/cpp_qt_learn --quick
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Los tests incluyen casos vacios, duplicados, limites de `int32_t`, muchos tamanos pequenos, datos aleatorios y una prueba intensiva con `1,000,000` elementos y `1,000,000` consultas.

## Hardware usado

Hardware registrado durante la ejecucion:

- CPU: AMD Ryzen AI 7 PRO 350 w/ Radeon 860M
- Nucleos logicos: 16
- RAM: 27.04 GiB

Las mediciones usan `std::chrono::steady_clock`.

## Ejemplo de resultado

Con `1,000,000` elementos, `1,000,000` consultas, `T = 3` y `B = 64` bytes:

| Estructura | Tiempo promedio | Bloques promedio |
| --- | ---: | ---: |
| Cache-oblivious static tree | 363.491 ms | 11.425 |
| BST con punteros | 464.454 ms | 15.035 |

Los resultados detallados se guardan en `results.csv`.
