# Projeto Apolo

**Versão 0.1.0 — base funcional validada manualmente no Windows 11 (x64).**

Apolo é um monitor de memória em C++/Win32, com limpeza manual conservadora de working sets de processos da conta e da sessão atuais. Código escrito independentemente, utilizando APIs documentadas do Windows.

## Recursos

- RAM usada, total, disponível e porcentagem (atualiza a cada 2,5 segundos).
- Memória comprometida (commit).
- Atualização sob demanda e limpeza manual com confirmação.
- Exclusão de processos essenciais, protegidos e do próprio Apolo.
- Sem rede, telemetria, atualizador ou elevação silenciosa.

## Compilar no Windows 11

Requer Visual Studio com **Desenvolvimento para desktop com C++**, Windows SDK e CMake.

No Visual Studio, abra a pasta do repositório como projeto CMake e selecione x64-Release; depois vá em Compilação > Compilar Tudo.

Ou no **Developer PowerShell for Visual Studio**:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

O executável estará normalmente em `build\Release\Apolo.exe` (gerador Visual Studio), ou na pasta do preset usado pela interface gráfica.

## Cuidados

Reduzir working sets **não garante melhora de desempenho** e pode provocar recarga de páginas e lentidão. Testar primeiro em uma máquina e avaliar antes de implantar na empresa.

A versão 0.2 (tray, histórico e configuração corporativa) ainda necessita validação no Windows e não faz parte desta base.

## Origem e licença

Implementação independente; não incorpora código-fonte do Mem Reduct nem da biblioteca routine. Definir a licença do Apolo antes de qualquer distribuição pública.
