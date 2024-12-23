# What's this project?

This project is to learn how attach a mach-o process to any other processes.

I made some C codes to learn attaching process, getting library images and injecting shell code to other processes etc.

NOTE: `inject-simple-shellcode.c` works correctly but `main.c` that is tyring to inject dylib does not work.

I recommend to refer inject way to below gist code.  
    -> https://gist.github.com/vocaeq/fbac63d5d36bc6e1d6d99df9c92f75dc  
This code show you that the injection works perfectly.

--------------

# このプロジェクトについて

このプロジェクトは Mach-O プロセスを外部にアタッチする方法、およびシェルコード挿入などの方法を学ぶためにテストするためのものです。

※ `inject-simple-shellcode.c` は正常に動作しますが、動的ライブラリの注入を試みている `main.c` は注入後にクラッシュする状態です。

動的ライブラリを注入して動作を確認したい場合は以下の Gist コードが完全に動くのでそちらを参照ください。  
https://gist.github.com/vocaeq/fbac63d5d36bc6e1d6d99df9c92f75dc