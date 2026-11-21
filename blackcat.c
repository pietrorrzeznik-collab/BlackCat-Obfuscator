#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <emscripten/emscripten.h>

void _p1(char *_b, int *_l) {
    const char _c[] = {'i', 'I', 'l', '_'};
    int _len = 500 + (rand() % 100);
    *_l = _len;

    for (int _i = 0; _i < _len; _i++) {
        _b[_i] = _c[rand() % 4];
    }
    _b[_len] = '\0';
}

void _p2(const char *_in, char *_out) {
    _out[0] = '\0';
    while (*_in) {
        unsigned char _byte = (unsigned char)*_in;
        char _bin[9];
        for (int _i = 7; _i >= 0; _i--) {
            _bin[7 - _i] = ((_byte >> _i) & 1) ? '1' : '0';
        }
        _bin[8] = '\0';
        strcat(_out, _bin);
        _in++;
    }
}

EMSCRIPTEN_KEEPALIVE
char* obfuscate_script(const char* _input) {
    srand((unsigned int)time(NULL));

    size_t _total = strlen(_input);
    char _v1[600], _v2[600];
    int _l1, _l2;

    _p1(_v1, &_l1);
    _p1(_v2, &_l2);

    size_t _bin_sz = _total * 8 + 1;
    char *_payload = (char *)malloc(_bin_sz);
    if (!_payload) return NULL;
    _p2(_input, _payload);

    /* FIX: o template usa _v1 duas vezes e _v2 duas vezes, alem do
       texto fixo. O tamanho antigo (+800) nao cobria isso e truncava
       o resultado. Agora reservamos espaco real para cada variavel
       mais uma margem generosa para o texto fixo do template. */
    size_t _res_sz = strlen(_payload)
                    + (size_t)_l1 * 2
                    + (size_t)_l2 * 2
                    + 512;

    char *_res = (char *)malloc(_res_sz);
    if (!_res) {
        free(_payload);
        return NULL;
    }

    snprintf(_res, _res_sz,
        "-- Protected By BlackCat.net/obfuscator\n"
        "local %s = \"%s\"\n"
        "local function %s(v)\n"
        "    local t = {}\n"
        "    for i = 1, #v, 8 do\n"
        "        local b = v:sub(i, i+7)\n"
        "        table.insert(t, string.char(tonumber(b, 2)))\n"
        "    end\n"
        "    return table.concat(t)\n"
        "end\n"
        "local _ = loadstring(%s(%s))()\n",
        _v1, _payload, _v2, _v2, _v1
    );

    free(_payload);
    return _res;
}

/* FIX: exportada para que o JS possa liberar a memoria do resultado
   depois de ler a string, evitando vazamento a cada clique. */
EMSCRIPTEN_KEEPALIVE
void free_result(char* ptr) {
    if (ptr) free(ptr);
}
