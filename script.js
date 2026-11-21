document.addEventListener('contextmenu', event => event.preventDefault());

document.addEventListener('keydown', function(event) {
    if (event.keyCode === 123) {
        event.preventDefault();
        return false;
    }
    if (event.ctrlKey && event.shiftKey && (event.keyCode === 73 || event.keyCode === 74 || event.keyCode === 67)) {
        event.preventDefault();
        return false;
    }
    if (event.ctrlKey && event.keyCode === 85) {
        event.preventDefault();
        return false;
    }
});

const editor = document.getElementById("codeEditor");
const obfuscateButton = document.getElementById("obfuscateButton");
const copyButton = document.getElementById("copyButton");

let cModule = null;
let obfuscateFunc = null;
let freeFunc = null;

if (typeof Module === "function") {
    Module().then(instance => {
        cModule = instance;
        // FIX: o retorno agora e lido como ponteiro ('number'), nao
        // como 'string' automatica, porque precisamos do endereco
        // para poder liberar a memoria depois.
        obfuscateFunc = cModule.cwrap('obfuscate_script', 'number', ['string']);
        freeFunc = cModule.cwrap('free_result', null, ['number']);
    });
}

function resetToObfuscateState() {
    obfuscateButton.hidden = false;
    copyButton.hidden = true;
    copyButton.textContent = "Copy";
}

// Se o usuario editar o texto depois de ofuscar, volta a mostrar o
// botao "Obfuscate" em vez de travar na tela de "Copy" ate dar F5.
editor.addEventListener("input", () => {
    if (!obfuscateButton.hidden) return;
    resetToObfuscateState();
});

obfuscateButton.addEventListener("click", async () => {
    const originalCode = editor.value;

    if (!originalCode.trim()) {
        alert("Paste a Luau script first!");
        return;
    }

    if (!cModule || !obfuscateFunc) {
        alert("Engine is still loading, wait a second.");
        return;
    }

    try {
        const resultPtr = obfuscateFunc(originalCode);

        if (!resultPtr) {
            alert("Error running the obfuscation engine.");
            return;
        }

        const obfuscatedCode = cModule.UTF8ToString(resultPtr);

        // FIX: libera a memoria alocada no C (malloc) depois de ler
        // a string, para nao vazar memoria a cada clique.
        freeFunc(resultPtr);

        editor.value = obfuscatedCode;
        obfuscateButton.hidden = true;
        copyButton.hidden = false;

    } catch (error) {
        alert("Error running the obfuscation engine.");
    }
});

copyButton.addEventListener("click", async () => {
    try {
        await navigator.clipboard.writeText(editor.value);
        copyButton.textContent = "Copied!";
        setTimeout(() => {
            copyButton.textContent = "Copy";
        }, 1500);
    } catch (error) {
        alert("Could not copy the code.");
    }
});
