
#!/bin/bash
#set -x
BIN=./exec_lines
PASS=0
FAIL=0

run_test() {
    DESC="$1"
    CMD="$2"
    EXPECT="$3"

    OUTPUT=$(eval "$CMD" 2>&1)
    STATUS=$?

    if [[ "$OUTPUT" == *"$EXPECT"* ]]; then
        echo "[PASS] $DESC"
        ((PASS++))
    else
        echo "[FAIL] $DESC"
        echo "  Comando: $CMD"
        echo "  Esperado: $EXPECT"
        echo "  Obtenido: $OUTPUT"
        ((FAIL++))
    fi
}

echo "===== INICIO TESTS ====="

# 1. Ayuda
run_test "Help (-h)" "$BIN -h" "Uso:"

# 2. Opción sin argumento
run_test "Falta argumento -b" "$BIN -b" "option requires an argument"

# 3. Buffer fuera de rango
run_test "Buffer inválido" "$BIN -b 0" "Error"

# 4. Procesos fuera de rango
run_test "Procesos inválidos" "$BIN -p 0" "Error"

# 5. Línea demasiado larga
run_test "Linea larga" "python3 genera_bytes.py -n 129 | base64 | tr -d '\n' | $BIN -l 128" "demasiado larga"

# 6. Ejecución simple
touch testfile
run_test "Comando simple" "echo ls testfile | $BIN" "testfile"

# 7. Múltiples líneas
run_test "Varias líneas" "echo -e 'ls testfile\nls testfile' | $BIN" "testfile"

# 8. Error en comando
run_test "Comando con error" "echo -e 'ls testfile\nls noexiste' | $BIN" "Error al ejecutar"

# 9. Pipe
run_test "Pipe" "echo 'ls testfile | wc -c' | $BIN" "9"

# 10. Redirección
run_test "Redirección" "echo 'ls testfile > out.txt' | $BIN && cat out.txt" "testfile"

# 11. Append
run_test "Append" "echo -e 'ls testfile >> out2.txt\nls testfile >> out2.txt' | $BIN && cat out2.txt" "testfile"

# 12. EOF sin salto
run_test "Sin newline final" "echo -n 'ls testfile' | $BIN" "testfile"

# 13. Comando inexistente
run_test "Comando inexistente" "echo 'comando_fake_123' | $BIN" "Error"

# 14. Concurrencia p=1 (~3s)
TIME1=$( (time -p echo -e "sleep 1\nsleep 1\nsleep 1" | $BIN -p 1) 2>&1 | grep real | awk '{print $2}' )
echo "[INFO] Tiempo p=1: $TIME1 s"

# 15. Concurrencia p=3 (~1s)
TIME2=$( (time -p echo -e "sleep 1\nsleep 1\nsleep 1" | $BIN -p 3) 2>&1 | grep real | awk '{print $2}' )
echo "[INFO] Tiempo p=3: $TIME2 s"

echo "===== RESULTADO ====="
echo "PASS: $PASS"
echo "FAIL: $FAIL"

if [ $FAIL -eq 0 ]; then
    echo "✅ TODOS LOS TESTS OK"
else
    echo "❌ HAY FALLOS"
fi

