# Projeto Apolo

**Versão atual: v0.6.0 (piloto) • Windows 11 x64 • C++20 nativo**

O **Apolo** monitora CPU, RAM, E/S de disco e processos da conta atual, com uma rotina opcional de redução do *working set* de processos acessíveis. É uma implementação independente: não utiliza código do Mem Reduct. Não possui serviços de rede, telemetria, navegador embutido, atualizador, serviço do Windows ou elevação silenciosa.

> **Aviso importante:** reduzir o *working set* pode aumentar a RAM física disponível, mas não garante computador mais rápido; pode causar recarga de páginas e lentidão. A limpeza automática é **desativada por padrão**. Ainda não houve validação funcional nem medição do consumo da v0.6 em Windows 11.

## v0.6 — Redesign nativo, elegante e leve

A interface foi reconstruída em **Win32 + Direct2D + DirectWrite**, sem Electron, WebView, Qt ou runtime pesado de interface. O objetivo é visual moderno com uso controlado de recursos; o consumo real só será conhecido após medições na máquina Windows.

- **Menu lateral** com Visão geral, Desempenho, Processos, Histórico e Configurações.
- **Identidade visual escura**: grafite/carvão, cards discretos, azul e ciano, tipografia Segoe UI, sem animações permanentes.
- **Visão geral:** cards RAM/CPU/Disco, gráfico compacto com CPU/RAM, diagnóstico e principais processos; ação explícita **Limpar memória**.
- **Desempenho:** gráficos integrados de CPU, RAM e taxa total do disco (MiB/s), com períodos 1, 5 e 15 minutos.
- **Processos:** até 24 processos acessíveis da **mesma conta e sessão**, com PID e *working set*, ordenação por memória, nome ou PID e rolagem. Não oferece encerramento de processos.
- **Histórico:** consulta interna das últimas operações registradas no arquivo CSV; opção **Abrir CSV** preservada.
- **Configurações:** limpeza automática, abertura na bandeja, limite de uso da RAM, intervalo mínimo e botão para salvar. Política corporativa local bloqueia alterações quando presente.
- **Bandeja do Windows:** fechar a janela oculta o programa, se o ícone estiver disponível; para encerrar use **Sair do Apolo** no menu de contexto do ícone.

### Decisões de baixo consumo

1. Interface desenhada via Direct2D/DirectWrite; objetos de desenho e fontes são reaproveitados.
2. Redesenho sob demanda e, enquanto a janela está aberta, após a coleta de métricas, sem animações a 60 FPS.
3. Amostras coletadas a cada ~2,5 segundos. Inventário de processos da conta reavaliado aproximadamente a cada 5 segundos.
4. Gráficos limitados a **360 amostras voláteis (~15 minutos)**; não gravam CPU/RAM/disco em disco.
5. A janela antiga de análise e os controles visuais antigos saíram da compilação; os dados passam a ser compartilhados com a interface principal.
6. Não é criado nenhum processo auxiliar para desenhar a interface.

**Limitação conhecida:** enquanto o aplicativo está em execução, a coleta continua mesmo com a janela oculta na bandeja. A v0.6 não usa temporização diferenciada para o modo oculto. Teclado e navegação assistiva na interface desenhada serão aprimorados antes de uma versão estável.

## Como atualizar e compilar

O GitHub é **somente o repositório de código**. Não temos compilação automática no GitHub Actions. Você atualiza e compila **localmente no Visual Studio**.

Primeiro clone:

~~~powershell
git clone https://github.com/LucasEspindula/project-apolo.git
cd project-apolo
~~~

Para atualizar uma pasta clonada:

~~~powershell
git pull origin main
~~~

**Antes de compilar**, encerre o Apolo pelo ícone da bandeja: botão direito → **Sair do Apolo**. Se o processo estiver aberto, o vinculador do Visual Studio pode gerar LNK1104 ao tentar substituir o Apolo.exe.

1. Abra a pasta project-apolo no Visual Studio.
2. Selecione **x64-Release**.
3. Pressione **Ctrl + Shift + B** (Compilação → Compilar Tudo).
4. Inicie Apolo.exe pelo Visual Studio ou em out\build\x64-Release\Apolo.exe.

Alternativa no **Developer PowerShell for Visual Studio**:

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\Apolo.exe
~~~

Requisitos: Windows 11 x64, Windows SDK, Visual Studio com **Desenvolvimento para desktop com C++**, CMake, MSVC.

## Recursos de monitoramento e seus limites

| Métrica | Fonte | Observação |
| --- | --- | --- |
| RAM total/usada/disponível | GlobalMemoryStatusEx | Porcentagem global aproximada |
| Memória comprometida | GetPerformanceInfo | Não é equivalente à RAM física |
| CPU | GetSystemTimes | Variação entre amostras; a primeira pode ficar indisponível |
| Disco | PDH, PhysicalDisk(_Total) | Leitura/gravação em MiB/s; não mede latência nem saturação |
| Processos | Toolhelp + GetProcessMemoryInfo | Apenas processos acessíveis da conta e sessão atuais |
| Diagnóstico | Regras locais sobre amostras recentes | Indicadores preliminares, **não** diagnóstico definitivo |

O diagnóstico procura sinais repetidos (pelo menos oito amostras válidas e condição presente em 75% delas no último minuto), por exemplo CPU muito alta ou RAM disponível baixa junto de commit alto. **Não** aciona automaticamente o limpador.

## Limpeza de memória

A ação manual solicita confirmação. A operação usa a API documentada **EmptyWorkingSet**, restringe a tentativa a processos acessíveis da mesma conta e sessão, exclui o próprio Apolo e processos centrais do Windows, sem elevação ou APIs não documentadas de limpeza de cache do kernel.

A automação já existente só funciona **se for ativada e salva** pelo usuário ou configurada explicitamente por uma política válida da TI. Limiares: **70, 75, 80, 85, 90 ou 95%** de RAM; intervalo: **15, 30, 60, 120 ou 240 minutos**. Ela é experimental: recomendamos manter desligada até medir impacto real no desempenho. O diagnóstico visual não controla essa automação.

Reduzir o working set pode gerar mais paginação e piorar a experiência. A variação registrada de RAM disponível não é medida de velocidade.

## Arquivos locais e política corporativa

| Arquivo | Local |
| --- | --- |
| Preferências do usuário | %LOCALAPPDATA%\Apolo\settings.ini |
| Histórico das limpezas | %LOCALAPPDATA%\Apolo\history.csv |
| Política da TI, quando utilizada | C:\ProgramData\Apolo\policy.ini |

A v0.6 mostra as últimas entradas do CSV na tela **Histórico**, sem criar um banco de dados nem armazenar nome dos processos. Não há limite automático de retenção do arquivo original. **Histórico gráfico e histórico de limpezas são coisas distintas**.

O modelo de política está em deployment/policy.ini, com limpeza automática desativada. A política, se presente, sobrepõe as opções locais. **As permissões NTFS da pasta/arquivo devem ser protegidas**: usuários somente leitura; administradores escrita. O script deployment/Install-Apolo.ps1 serve apenas para instalação piloto e **não** define ACLs, serviço, tarefa ou inicialização automática.

## Estrutura do projeto

~~~text
project-apolo/
├── src/
│   ├── main.cpp          # Win32, limpeza e integração
│   ├── modern_ui.h/.cpp   # Direct2D/DirectWrite e navegação
│   ├── performance.h/.cpp # CPU, disco e processos
│   └── analysis.h/.cpp    # histórico volátil e sinais de atenção
├── deployment/
│   ├── policy.ini
│   └── Install-Apolo.ps1
├── CMakeLists.txt
├── CMakeSettings.json
└── README.md
~~~

## Checklist de validação da v0.6 — faça no Windows 11

- [ ] Compilar **Release x64** localmente; conferir que Apolo.exe abre sem erro.
- [ ] Verificar aspecto escuro, menu lateral, cards, alinhamento e redimensionamento (incluindo DPI/escala do Windows).
- [ ] Conferir leituras de RAM/CPU/disco e valores dos gráficos após alguns ciclos.
- [ ] Alternar 1/5/15 minutos e comparar a direção dos gráficos com o Gerenciador de Tarefas.
- [ ] Consultar Processos: ordenar por RAM/nome/PID e rolar.
- [ ] Abrir Histórico e comparar linhas com %LOCALAPPDATA%\Apolo\history.csv.
- [ ] Testar as opções (sem ativar automação no piloto), salvar e reiniciar o programa.
- [ ] Executar uma limpeza manual com confirmação e conferir o histórico.
- [ ] Fechar na bandeja, reabrir e encerrar pelo menu Sair.
- [ ] **Medir o consumo do próprio Apolo** (RAM privada/working set, CPU ao longo de minutos, responsividade e velocidade de inicialização), comparando com a v0.5. O produto não deve piorar a experiência do usuário.

**Não distribuir nas 11–30 máquinas antes dos testes**, revisão de segurança, verificação de permissões, antivírus, assinatura e plano de recuperação.

## Histórico de versões

- **v0.1:** monitoramento de RAM e limpeza manual, testada em Windows 11.
- **v0.2:** bandeja, INI, automação opcional, CSV e política corporativa piloto.
- **v0.3:** CPU, atividade de disco e lista de processos.
- **v0.4:** gráficos locais, diagnóstico por histórico e lista ampliada.
- **v0.5:** abas e barras visuais.
- **v0.6 (piloto):** redesign nativo dark, navegação lateral, cards e gráficos integrados, lista e histórico na mesma interface, removendo o código de UI anterior.

## Próximas etapas

Validar a v0.6, corrigir problemas visuais e medir consumo; melhorar acessibilidade, DPI e coleta ao ficar na bandeja antes de adicionar novas rotinas de desempenho ou distribuir corporativamente.

A licença para distribuição pública ainda precisa ser definida.
