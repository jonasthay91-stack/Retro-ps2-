# Plano visual — como o RetroHub fica bom de verdade

> O objetivo não é imitar o XMB nem um lançador de Android. É ficar melhor que os dois **dentro
> desta tela**, usando o que o GS faz bem em vez de lamentar o que ele não faz.

---

## 1. O que o hardware realmente permite

O GS do PS2 é de função fixa: não há shader, então não há desfoque, onda animada, sombra suave nem
iluminação. Em compensação ele tem **muita capacidade de preenchimento e mistura alfa excelente**,
e a 480 linhas o total de pixels é um quarto de uma tela 1080p.

| Técnica | Custo | Efeito |
|---|---|---|
| Mistura alfa | **área**, não chamadas | camadas, véus, sumiço suave |
| Ampliar textura | grátis | **desfoque** — o filtro bilinear borra sozinho |
| Espelhar em V | 1 primitiva | reflexo |
| Escala não uniforme | grátis | virar/inclinar sem 3D |
| Modulação de cor | grátis | escurecer, tingir, apagar |
| Sprites pequenos | baratíssimo | partículas, filetes, pontos |

**A regra que orienta tudo:** o que derruba a taxa de quadros é a **área pintada com alfa ligado**,
não o número de chamadas. Cinquenta retângulos de 20×20 custam menos que uma faixa de 640×40.

### O que está fora de alcance, e não adianta insistir

Desfoque de verdade, sombra com esfumaçado, gradiente contínuo (só empilhando faixas), rotação
arbitrária, texto girado, transparência por pixel em imagem paletizada.

---

## 2. A ordem importa mais que a lista

Acabamento tem hierarquia. Cada item abaixo só compensa se o anterior estiver resolvido — enfeite
sobre base instável **piora** a percepção.

```
  1. Fluidez        60 quadros travados, sem engasgo
  2. Movimento      nada salta; tudo desacelera, escalonado
  3. Tipografia     hierarquia, respiro, alinhamento
  4. Profundidade   camadas, reflexo, paralaxe
  5. Continuidade   transições entre telas, estado ocioso
  6. Som            navegação e ambiente
  7. Nitidez        480p quando houver
  8. Riqueza        metadados, selos, categorias
```

Uma interface simples que desliza a 60 quadros parece mais cara que uma cheia de efeitos que
engasga. **Fluidez é acabamento**, não pré-requisito dele.

---

## 3. As etapas

### V1 — Base sólida *(em andamento)*

- [x] Área pintada por quadro cortada pela metade (3 telas → 1,5)
- [x] Capa carregando com cache próprio e chave por jogo
- [x] Dois corpos de fonte, hierarquia mínima
- [x] Mola no foco, texto escalonado, fora de foco escurecido
- [ ] **Confirmar 60 quadros estáveis no console** ← porta para tudo abaixo
- [ ] Contadores de quadro e de área no build de depuração

### V2 — Profundidade

**Reflexo sob a capa.** `rmDrawQuad` com V invertido no quad espelha a textura; por cima, 4 a 6
faixas de alfa crescente apagam o reflexo de baixo para cima. Custo: 1 primitiva + faixas pequenas.
Atenção: `rmDrawQuad` recebe coordenadas **já escaladas** — precisa passar por `rmScaleX`/`rmScaleY`,
diferente de `rmDrawPixmap`.

**Paralaxe do fundo.** A capa de fundo desloca alguns pixels no sentido contrário ao da fila. Mesma
primitiva, coordenadas diferentes: **custo zero** e a sensação de duas camadas de profundidade.

**A capa virando.** Ao mudar a seleção, a capa encolhe em X até quase nada e volta — escala não
uniforme dá a leitura de "virou". É a ideia original da conversa: folhear um livro e a capa se voltar
para quem olha.

**Sombra em duas camadas** com deslocamentos diferentes já existe; falta afinar o alfa por tamanho.

### V3 — Continuidade

**Transição entre telas.** Hoje a troca lista↔estante é um corte seco. Um esmaecimento de 8 quadros
sobre preto, ou um deslize lateral, custa uma faixa de tela cheia durante poucos quadros — aceitável
porque é passageiro.

**Estado ocioso.** Sem toque por alguns segundos, o fundo passa a derivar muito devagar. Tela que se
mexe sozinha nunca parece travada, e é justamente o que separa "ligado" de "congelado".

**Estados de carregamento.** Enquanto a capa vem do USB, a moldura pulsa de leve em vez de ficar
vazia. Espera com sinal de vida parece mais curta que espera muda.

### V4 — Som

O mais subestimado de todos. O OPL já tem `sfxPlay` e um banco de efeitos.

- Clique curto e seco na navegação (já existe, falta afinar volume)
- Som distinto ao confirmar
- Música ambiente opcional, em volume baixo, silenciável

Som de navegação bem calibrado faz mais pela sensação de qualidade que qualquer reflexo.

### V5 — Nitidez

`rmSetMode` expõe 14 modos de vídeo. **Se o console e a TV suportarem 480p**, o texto deixa de
tremer e linhas de 1 px passam a existir — é o maior ganho isolado de nitidez possível, e não custa
desempenho nenhum.

Enquanto for 480i: nada de linha horizontal de 1 px (cintila), nada de texto abaixo de ~14 px, e
contraste sempre alto.

### V6 — Riqueza

Depende da Fase 2 (índice e metadados): ano, gênero, jogadores, região, compatibilidade, última
partida. Selos pequenos e alinhados valem mais que texto corrido — e sprite pequeno é o que o GS
faz mais barato.

---

## 4. Onde ganhamos de um lançador de Android barato

Não em resolução nem em efeito. Em três coisas que dependem de decisão, não de hardware:

1. **Não há atraso de software por cima.** Sem sistema operacional disputando, sem coleta de lixo,
   sem camada de compatibilidade. Aperta o botão, mexe no mesmo quadro.
2. **A arte é o conteúdo.** Uma capa de 192×276 bem recortada preenche a tela de personalidade. O
   lançador não precisa inventar beleza — só emoldurar bem a que já existe.
3. **Restrição gera coerência.** Quatro primitivas obrigam a um sistema visual consistente. Interface
   feia quase sempre é interface sem sistema, não interface sem recurso.

---

## 5. O que não vamos fazer

- **Copiar o XMB.** É desenho da Sony, e clone não vira identidade.
- **Efeito que derrube quadro.** Qualquer coisa que custe fluidez sai, por mais bonita que seja.
- **Enfeite antes de medir.** Depois da V1 entra instrumentação; daí em diante cada efeito é aprovado
  com número, não com impressão.
