# Histórico de mudanças

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e as versões seguem o [Versionamento Semântico](https://semver.org/lang/pt-BR/).

## 1.1.1 (12/10/2026)

### Corrigido

- Gravações longas sem mexer no PC não terminam mais por causa da suspensão automática do Windows: durante a gravação, o PC não suspende e a tela não apaga sozinha.
- Tela bloqueada (Win+L, Ctrl+Alt+Del) ou janela de permissão do Windows no meio da gravação: a gravação pausa sozinha e continua quando a tela volta, em vez de ser encerrada.
- O programa finaliza a gravação a tempo de montar o vídeo quando o disco começa a encher (antes, podia faltar espaço na montagem final).
- Trechos interrompidos à força não se sobrepõem mais ao trecho seguinte no vídeo final.

### Alterado

- A leitura dos trechos na montagem final não tem mais tempo máximo e mostra o progresso, para gravações de muitas horas.

## 1.1.0 (12/10/2026)

### Adicionado

- Botão **Mic** na barra de controle para silenciar e reativar só o microfone durante a gravação. O vídeo e o som do PC continuam gravando. Também funciona pela tecla **F8**, pelo botão **Silenciar mic** da janela e pelo menu do ícone perto do relógio.
- Atalho do microfone configurável em Configurações > Atalhos.
- **Mistura: som do PC x sua voz** em Configurações > Áudio, com as opções Equilibrado, Comentar vídeo (som do PC 30%, voz 150%) e Destacar o PC (voz 60%).
- O quadro de áudio da tela principal mostra o volume quando ele não está em 100%.

### Alterado

- Volumes do som do PC e do microfone de 0 a 300%, com limitador automático na montagem final para o som não distorcer.
- O áudio temporário é gravado em 16 bits, ocupando metade do espaço em disco durante a gravação.
- O medidor do microfone fica laranja quando ele está mudo.

## 1.0.0 (11/10/2026)

### Adicionado

- Gravação da tela inteira (qualquer monitor) ou de uma área livre, com seleção por arraste, clique numa janela ou teclado.
- Barra flutuante com tempo, pausar e finalizar, invisível no vídeo.
- Pausar e continuar num arquivo só, com o áudio sincronizado.
- MP4 (H.264/H.265 + AAC) e AVI (MPEG-4/Xvid, Motion JPEG ou H.264 + MP3/PCM).
- Codificação pela placa de vídeo (NVENC, Quick Sync e AMF), com volta automática para o processador.
- Resoluções de 360p a 4K, mais a personalizada "2060p", e 15, 24, 30 ou 60 FPS.
- Áudio do sistema e microfone, com escolha do aparelho, volume e medidores.
- Atalhos globais, contagem regressiva, notificações e lista de gravações.
- Recuperação de gravações interrompidas e modo portátil.
