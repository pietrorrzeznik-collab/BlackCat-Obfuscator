#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <emscripten/emscripten.h>

#define NUM_DECOYS 8
#define NUM_SECRETS 7
#define FAKE_GROUPS 3
#define MAX_LEAVES 5

/* === Gerador de homoglifos: nomes de variavel usando apenas
   i, I, l, _, O, 0 misturados, 500 a 600 caracteres === */
void _p1(char *_b, int *_l) {
    const char _c[] = {'i', 'I', 'l', '_', 'O', '0'};
    int _len = 500 + (rand() % 101); /* 500 a 600 (inclusive) */
    *_l = _len;
    /* identificador Lua nao pode comecar com digito, entao o
       primeiro caractere nunca pode ser '0' (indice 5 em _c) */
    _b[0] = _c[rand() % 5];
    for (int _i = 1; _i < _len; _i++) {
        _b[_i] = _c[rand() % 6];
    }
    _b[_len] = '\0';
}

/* === Mais formatos de payload: decimal, hexadecimal ou octal,
   escolhido aleatoriamente a cada geracao, com separador tambem
   sorteado === */
typedef enum { FMT_DEC = 0, FMT_HEX = 1, FMT_OCT = 2 } PayloadFormat;

void _format_num(unsigned int _val, PayloadFormat _fmt, char *_tmp, size_t _tmp_sz) {
    const char *_f = (_fmt == FMT_HEX) ? "%x" : (_fmt == FMT_OCT) ? "%o" : "%u";
    snprintf(_tmp, _tmp_sz, _f, _val);
}

void _p2_encode(const char *_in, size_t _len, unsigned char _key,
                 PayloadFormat _fmt, char _sep, char *_out, size_t _out_sz) {
    size_t _pos = 0;
    for (size_t _i = 0; _i < _len; _i++) {
        unsigned char _enc = ((unsigned char)_in[_i]) ^ _key;
        char _tmp[8];
        _format_num(_enc, _fmt, _tmp, sizeof(_tmp));
        if (_i > 0) {
            if (_pos + 1 >= _out_sz) break;
            _out[_pos++] = _sep;
        }
        size_t _tl = strlen(_tmp);
        if (_pos + _tl >= _out_sz) break;
        memcpy(_out + _pos, _tmp, _tl);
        _pos += _tl;
    }
    _out[_pos] = '\0';
}

void _p3_fake(char *_out, size_t _out_sz, int _count, PayloadFormat _fmt, char _sep) {
    size_t _pos = 0;
    for (int _i = 0; _i < _count; _i++) {
        char _tmp[8];
        _format_num((unsigned int)(rand() % 256), _fmt, _tmp, sizeof(_tmp));
        if (_i > 0) {
            if (_pos + 1 >= _out_sz) break;
            _out[_pos++] = _sep;
        }
        size_t _tl = strlen(_tmp);
        if (_pos + _tl >= _out_sz) break;
        memcpy(_out + _pos, _tmp, _tl);
        _pos += _tl;
    }
    _out[_pos] = '\0';
}

/* === expressoes aritmeticas aninhadas: em vez de uma soma simples
   "A + B + C", cada numero-semente agora vira uma conta com varios
   termos, parenteses e multiplicacao, tipo:
   vKey = ((P1 * P2 + P3) - (P4 * P5)) + CORRETIVA
   Os P's sao numeros pequenos e aleatorios (sem significado); a
   ultima variavel (CORRETIVA) e calculada para fechar a conta no
   valor exato que precisamos. So uso +, - e *: nunca divisao, para
   nao correr risco de gerar numero quebrado (o XOR em Lua exige
   inteiro exato). === */
typedef struct {
    int template_id;              /* 0, 1 ou 2 */
    char leafNames[MAX_LEAVES][601];
    long long leafValues[MAX_LEAVES]; /* FIX: "long" e so 32 bits no
        Emscripten/wasm32; "long long" garante 64 bits de verdade,
        evitando overflow com valores maiores (ex: checksum de 32 bits) */
    int leafCount;
    char correctiveName[601];
    long long correctiveValue;
} ExprBlock;

void _build_expr_block(long long _target, ExprBlock *_out) {
    _out->template_id = rand() % 3;
    long long _v[MAX_LEAVES];
    int _len;

    if (_out->template_id == 0) {
        /* ((P1 * P2 + P3) - (P4 * P5)) + CORRETIVA */
        _out->leafCount = 5;
        for (int _i = 0; _i < 5; _i++) {
            _v[_i] = (rand() % 19) - 9; /* -9 a 9 */
            _out->leafValues[_i] = _v[_i];
            _p1(_out->leafNames[_i], &_len);
        }
        long long _pre = (_v[0] * _v[1] + _v[2]) - (_v[3] * _v[4]);
        _out->correctiveValue = _target - _pre;
    } else if (_out->template_id == 1) {
        /* (((P1 - P2) + (P3 - P4)) - P5) + CORRETIVA */
        _out->leafCount = 5;
        for (int _i = 0; _i < 5; _i++) {
            _v[_i] = (rand() % 19) - 9;
            _out->leafValues[_i] = _v[_i];
            _p1(_out->leafNames[_i], &_len);
        }
        long long _pre = ((_v[0] - _v[1]) + (_v[2] - _v[3])) - _v[4];
        _out->correctiveValue = _target - _pre;
    } else {
        /* ((P1 + P2) - (P3 * UM)) + CORRETIVA, onde UM e uma variavel
           "isca" que so existe pra multiplicar por 1 e parecer que faz
           diferenca, sem mudar o valor de verdade */
        _out->leafCount = 4;
        for (int _i = 0; _i < 3; _i++) {
            _v[_i] = (rand() % 19) - 9;
            _out->leafValues[_i] = _v[_i];
            _p1(_out->leafNames[_i], &_len);
        }
        _v[3] = 1; /* "UM", sempre 1 */
        _out->leafValues[3] = 1;
        _p1(_out->leafNames[3], &_len);
        long long _pre = (_v[0] + _v[1]) - (_v[2] * _v[3]);
        _out->correctiveValue = _target - _pre;
    }
    _p1(_out->correctiveName, &_len);
}

/* embaralhamento Fisher-Yates, usado para variar a ordem em que os
   blocos (numeros reais, blocos-isca "matematicos" e variaveis-isca
   de string) aparecem no script gerado */
void _shuffle(int *_arr, int _n) {
    for (int _i = _n - 1; _i > 0; _i--) {
        int _j = rand() % (_i + 1);
        int _t = _arr[_i];
        _arr[_i] = _arr[_j];
        _arr[_j] = _t;
    }
}

typedef enum { ITEM_SECRET = 0, ITEM_FAKE = 1, ITEM_DECOY = 2 } ItemType;
typedef struct {
    ItemType type;
    int idx;
} Item;

EMSCRIPTEN_KEEPALIVE
char* obfuscate_script(const char* _input) {
    /* FIX: antes isso rodava "srand(time(NULL))" TODA VEZ que a
       funcao era chamada, e time() so tem resolucao de 1 segundo -
       se o usuario clicasse em "Obfuscate" duas vezes rapido, saia
       EXATAMENTE o mesmo resultado (mesma chave, mesmos nomes).
       Agora so semeia uma unica vez (na primeira chamada); depois
       disso o gerador so continua avancando, sempre diferente. */
    static int _seeded = 0;
    if (!_seeded) {
        srand((unsigned int)time(NULL));
        _seeded = 1;
    }
    size_t _total = strlen(_input);

    /* sorteia o formato do payload (decimal/hex/octal) e o separador */
    PayloadFormat _fmt = (PayloadFormat)(rand() % 3);
    const char _seps[] = {'/', ',', '-', '|'};
    char _sep = _seps[rand() % 4];
    const char *_pattern = (_fmt == FMT_HEX) ? "%x+" : (_fmt == FMT_OCT) ? "[0-7]+" : "%d+";
    long long _baseVal = (_fmt == FMT_HEX) ? 16 : (_fmt == FMT_OCT) ? 8 : 10;

    /* chave XOR dinamica e chave da tabela "LOL" */
    unsigned char _key = (unsigned char)(1 + (rand() % 255));
    unsigned long _tkeyVal = 1000 + (unsigned long)(rand() % 900000);

    /* FIX: checksum melhorado - antes era uma soma simples (bytes
       diferentes podiam gerar o mesmo resultado); agora e um hash
       multiplicativo (soma = soma*31 + byte, a cada byte), bem mais
       resistente a colisao. "unsigned long" e 32 bits no Emscripten,
       entao o overflow natural dele ja funciona como um "% 2^32"
       embutido - nao precisa escrever a conta explicitamente */
    unsigned long _checksum = 0;
    for (size_t _i = 0; _i < _total; _i++) {
        _checksum = _checksum * 31 + (unsigned char)_input[_i];
    }

    /* nomes-homoglifo das variaveis "reais" nao-numericas */
    char _vPayload[601], _vTable[601];
    int _lPayload, _lTable;
    _p1(_vPayload, &_lPayload);
    _p1(_vTable, &_lTable);

    /* nomes-homoglifo das 7 variaveis "reais" que guardam os valores
       finais: chave, checksum, chave-da-tabela, "1", o modulo do
       checksum (agora 2^32, nao mais 65536), a base numerica
       (10/16/8) e o multiplicador do novo checksum (31) */
    char _vKey[601], _vChk[601], _vTKey[601], _vOne[601], _vMod[601], _vBase[601], _vMul[601];
    int _lKey, _lChk, _lTKey, _lOne, _lMod, _lBase, _lMul;
    _p1(_vKey, &_lKey);
    _p1(_vChk, &_lChk);
    _p1(_vTKey, &_lTKey);
    _p1(_vOne, &_lOne);
    _p1(_vMod, &_lMod);
    _p1(_vBase, &_lBase);
    _p1(_vMul, &_lMul);

    /* nomes-homoglifo das variaveis INTERNAS da funcao de decode */
    char _vParam[601], _vBytes[601], _vNum[601], _vT[601];
    char _vI1[601], _vS[601], _vSum[601], _vI2[601];
    int _lParam, _lBytes, _lNum, _lT, _lI1, _lS, _lSum, _lI2;
    _p1(_vParam, &_lParam);
    _p1(_vBytes, &_lBytes);
    _p1(_vNum, &_lNum);
    _p1(_vT, &_lT);
    _p1(_vI1, &_lI1);
    _p1(_vS, &_lS);
    _p1(_vSum, &_lSum);
    _p1(_vI2, &_lI2);

    /* monta as expressoes aritmeticas aninhadas dos 7 numeros-semente
       de verdade. _checksum e "unsigned long" (32 bits, pode chegar
       perto de 4,3 bilhoes) - por isso o _build_expr_block trabalha
       com "long long" (64 bits de verdade), senao essa conversao
       poderia estourar um "long" comum de 32 bits */
    ExprBlock _secretExpr[NUM_SECRETS];
    const char *_secretTargets[NUM_SECRETS];
    _build_expr_block((long long)_key, &_secretExpr[0]);        _secretTargets[0] = _vKey;
    _build_expr_block((long long)_checksum, &_secretExpr[1]);   _secretTargets[1] = _vChk;
    _build_expr_block((long long)_tkeyVal, &_secretExpr[2]);    _secretTargets[2] = _vTKey;
    _build_expr_block(1LL, &_secretExpr[3]);                    _secretTargets[3] = _vOne;
    _build_expr_block(4294967296LL, &_secretExpr[4]);           _secretTargets[4] = _vMod;
    _build_expr_block(_baseVal, &_secretExpr[5]);               _secretTargets[5] = _vBase;
    _build_expr_block(31LL, &_secretExpr[6]);                   _secretTargets[6] = _vMul;

    /* blocos-isca "matematicos": a MESMA estrutura de expressao
       aninhada acima, so que o resultado nunca e usado depois -
       impossivel diferenciar visualmente de um bloco real */
    ExprBlock _fakeExpr[FAKE_GROUPS];
    char _fakeTargets[FAKE_GROUPS][601];
    int _lFakeTarget[FAKE_GROUPS];
    for (int _f = 0; _f < FAKE_GROUPS; _f++) {
        /* FIX: gerar o valor-isca assim evita qualquer risco de
           comportamento indefinido por overflow (o resultado nunca e
           usado de verdade, mas nao custa nada ser limpo) */
        long long _fakeValue = (long long)(rand() % 2000001) - 1000000;
        _build_expr_block(_fakeValue, &_fakeExpr[_f]);
        _p1(_fakeTargets[_f], &_lFakeTarget[_f]);
    }

    /* variaveis-isca de string, no mesmo formato/separador do payload
       real, nunca usadas de verdade */
    char _vDecoy[NUM_DECOYS][601];
    int _lDecoy[NUM_DECOYS];
    char _fakeDecoy[NUM_DECOYS][256];
    for (int _d = 0; _d < NUM_DECOYS; _d++) {
        _p1(_vDecoy[_d], &_lDecoy[_d]);
        _p3_fake(_fakeDecoy[_d], sizeof(_fakeDecoy[_d]), 10 + (rand() % 50), _fmt, _sep);
    }

    size_t _payload_sz = _total * 4 + 8;
    char *_payload = (char *)malloc(_payload_sz);
    if (!_payload) return NULL;
    _p2_encode(_input, _total, _key, _fmt, _sep, _payload, _payload_sz);

    /* embaralha a ordem em que os 6 blocos reais, os blocos-isca
       matematicos e as variaveis-isca de string vao aparecer */
    int _totalItems = NUM_SECRETS + FAKE_GROUPS + NUM_DECOYS;
    Item _items[NUM_SECRETS + FAKE_GROUPS + NUM_DECOYS];
    int _ii = 0;
    for (int _s = 0; _s < NUM_SECRETS; _s++) { _items[_ii].type = ITEM_SECRET; _items[_ii].idx = _s; _ii++; }
    for (int _f = 0; _f < FAKE_GROUPS; _f++) { _items[_ii].type = ITEM_FAKE;   _items[_ii].idx = _f; _ii++; }
    for (int _d = 0; _d < NUM_DECOYS; _d++)  { _items[_ii].type = ITEM_DECOY;  _items[_ii].idx = _d; _ii++; }
    int _order[NUM_SECRETS + FAKE_GROUPS + NUM_DECOYS];
    for (int _i = 0; _i < _totalItems; _i++) _order[_i] = _i;
    _shuffle(_order, _totalItems);

    /* estima o tamanho do buffer final, com margem bem generosa */
    size_t _namesLen = (size_t)_lPayload + _lTable + _lKey + _lChk + _lTKey + _lOne + _lMod + _lBase + _lMul
                     + _lParam + _lBytes + _lNum + _lT + _lI1 + _lS + _lSum + _lI2;
    for (int _s = 0; _s < NUM_SECRETS; _s++) {
        _namesLen += strlen(_secretExpr[_s].correctiveName);
        for (int _p = 0; _p < _secretExpr[_s].leafCount; _p++) {
            _namesLen += strlen(_secretExpr[_s].leafNames[_p]);
        }
    }
    for (int _f = 0; _f < FAKE_GROUPS; _f++) {
        _namesLen += (size_t)_lFakeTarget[_f] + strlen(_fakeExpr[_f].correctiveName);
        for (int _p = 0; _p < _fakeExpr[_f].leafCount; _p++) {
            _namesLen += strlen(_fakeExpr[_f].leafNames[_p]);
        }
    }
    size_t _res_sz = strlen(_payload) + _namesLen * 6 + 8192;
    for (int _d = 0; _d < NUM_DECOYS; _d++) {
        _res_sz += (size_t)_lDecoy[_d] + strlen(_fakeDecoy[_d]) + 64;
    }

    char *_res = (char *)malloc(_res_sz);
    if (!_res) {
        free(_payload);
        return NULL;
    }

    size_t _pos = 0;
#define APPEND(fmt, ...) do { \
        int _w = snprintf(_res + _pos, _res_sz - _pos, fmt, ##__VA_ARGS__); \
        if (_w < 0 || (size_t)_w >= _res_sz - _pos) { free(_res); return NULL; } \
        _pos += (size_t)_w; \
    } while (0)

    APPEND("-- Protected By https://pietrorrzeznik-collab.github.io/BlackCat-Obfuscator/\n");

    /* declara TODOS os nomes de uma vez (sem valor ainda) */
    APPEND("local %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s",
           _vPayload, _vTable, _vKey, _vChk, _vTKey, _vOne, _vMod, _vBase, _vMul,
           _vParam, _vBytes, _vNum, _vT, _vI1, _vS, _vSum, _vI2);
    for (int _s = 0; _s < NUM_SECRETS; _s++) {
        for (int _p = 0; _p < _secretExpr[_s].leafCount; _p++) {
            APPEND(", %s", _secretExpr[_s].leafNames[_p]);
        }
        APPEND(", %s", _secretExpr[_s].correctiveName);
    }
    for (int _f = 0; _f < FAKE_GROUPS; _f++) {
        APPEND(", %s", _fakeTargets[_f]);
        for (int _p = 0; _p < _fakeExpr[_f].leafCount; _p++) {
            APPEND(", %s", _fakeExpr[_f].leafNames[_p]);
        }
        APPEND(", %s", _fakeExpr[_f].correctiveName);
    }
    APPEND(";");

    /* emite cada expressao aninhada, usando o template certo */
    for (int _i = 0; _i < _totalItems; _i++) {
        Item _it = _items[_order[_i]];
        if (_it.type == ITEM_SECRET || _it.type == ITEM_FAKE) {
            ExprBlock *_eb = (_it.type == ITEM_SECRET) ? &_secretExpr[_it.idx] : &_fakeExpr[_it.idx];
            const char *_tgt = (_it.type == ITEM_SECRET) ? _secretTargets[_it.idx] : _fakeTargets[_it.idx];
            for (int _p = 0; _p < _eb->leafCount; _p++) {
                APPEND("%s = %lld;", _eb->leafNames[_p], _eb->leafValues[_p]);
            }
            APPEND("%s = %lld;", _eb->correctiveName, _eb->correctiveValue);
            const char **_n = (const char **)_eb->leafNames;
            if (_eb->template_id == 0) {
                APPEND("%s = ((%s * %s + %s) - (%s * %s)) + %s;",
                       _tgt, _eb->leafNames[0], _eb->leafNames[1], _eb->leafNames[2],
                       _eb->leafNames[3], _eb->leafNames[4], _eb->correctiveName);
            } else if (_eb->template_id == 1) {
                APPEND("%s = (((%s - %s) + (%s - %s)) - %s) + %s;",
                       _tgt, _eb->leafNames[0], _eb->leafNames[1], _eb->leafNames[2],
                       _eb->leafNames[3], _eb->leafNames[4], _eb->correctiveName);
            } else {
                APPEND("%s = ((%s + %s) - (%s * %s)) + %s;",
                       _tgt, _eb->leafNames[0], _eb->leafNames[1], _eb->leafNames[2],
                       _eb->leafNames[3], _eb->correctiveName);
            }
            (void)_n;
        } else {
            APPEND("local %s = \"%s\";", _vDecoy[_it.idx], _fakeDecoy[_it.idx]);
        }
    }

    /* payload ja criptografado em XOR, no formato/separador sorteados */
    APPEND("%s = \"%s\";", _vPayload, _payload);
    free(_payload);

    /* estrutura "LOL": a funcao de decode vive dentro de uma tabela,
       acessada por uma chave numerica tambem ofuscada */
    APPEND("%s = {};", _vTable);
    /* FIX: logo depois de "do", "then" ou da abertura do corpo da
       funcao, nao pode vir um ";" sozinho sem nenhum comando antes -
       isso e valido em Lua 5.2+ ("statement vazio"), mas o Luau segue
       a gramatica mais restrita do Lua 5.1, que nao aceita. Por isso,
       nesses 4 pontos especificos, uso um espaco em vez de ";" (ainda
       serve de separador, so que sem criar um "statement vazio") */
    APPEND("%s[%s] = function(%s) ", _vTable, _vTKey, _vParam);
    APPEND("local %s = {};", _vBytes);
    APPEND("for %s in %s:gmatch(\"%s\") do ", _vNum, _vParam, _pattern);
    APPEND("%s[#%s+1] = tonumber(%s, %s);", _vBytes, _vBytes, _vNum, _vBase);
    APPEND("end;");
    APPEND("local %s = {};", _vT);
    APPEND("for %s = %s, #%s do ", _vI1, _vOne, _vBytes);
    APPEND("%s[%s] = string.char(%s[%s] ~ %s);", _vT, _vI1, _vBytes, _vI1, _vKey);
    APPEND("end;");
    APPEND("local %s = table.concat(%s);", _vS, _vT);
    APPEND("local %s = 0;", _vSum);
    APPEND("for %s = %s, #%s do %s = (%s * %s + string.byte(%s, %s)) %% %s end;",
           _vI2, _vOne, _vS, _vSum, _vSum, _vMul, _vS, _vI2, _vMod);
    APPEND("if %s ~= %s then ", _vSum, _vChk);
    APPEND("while true do end;");
    APPEND("end;");
    APPEND("return %s;", _vS);
    APPEND("end;");
    APPEND("local _ = loadstring(%s[%s](%s))();", _vTable, _vTKey, _vPayload);

#undef APPEND
    return _res;
}

EMSCRIPTEN_KEEPALIVE
void free_result(char* ptr) {
    if (ptr) free(ptr);
}
