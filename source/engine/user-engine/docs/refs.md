# References

KomoringHeights では、df-pn+アルゴリズム[^df-pn] [^df-pn-plus]をベースに探索を行う。df-pnアルゴリズムは詰将棋の求解ととても相性の良いAND/OR木の探索アルゴリズムで、df-pn+アルゴリズムはその改良版である。また、探索性能の改善を目的に末端接点における固定深さの探索[^fix-depth-search]をはじめとした高速化を施している。

また、コンパイルオプションを切り替えることでdeep df-pnアルゴリズム[^deep-dfpn]を適用することもできる。

詰将棋探索では、局面の優等関係の利用が非常に重要になる[^superiority]。置換表のLook Up時に現局面よりも持ち駒が多い or 少ない局面も同時に参照して探索に利用する。

df-pn系のアルゴリズムでは、次の3つの問題が常につきまとう。

- GHI問題（Graph History Interaction Problem）
- 無限ループ
- 証明数／反証数の二重カウント

まず、GHI問題に対しては[^ghi]を参考にオリジナルのアルゴリズムを用いて回避している。具体的には、千日手だけを扱う経路ハッシュテーブルを保持しておき、千日手の可能性がある局面では常にこれを参照することで千日手を検出している。

次に、無限ループ対策はTCA(Threshold Controlling Algorithm) [^tca-and-double-count]を採用している。これは、無限ループの可能性がある局面で探索を延長して問題を回避するアルゴリズムである。

また、証明数／反証数の二重カウント対策には[^df-pn] [^tca-and-double-count]を用いている。

また、開発に際して以下の情報を参考にした。

- 1局面の合法王手の最大数[^legal-check]
- やねうら王 詰将棋500万問問題集[^yane-5-million]

[^superiority]: 脊尾昌宏. (1999). 詰将棋を解くアルゴリズムにおける優越関係の効率的な利用について. ゲームプログラミングワークショップ 1999 論文集, 1999(14), 129-136.
[^df-pn]: 長井歩, & 今井浩. (2002). df-pn アルゴリズムの詰将棋を解くプログラムへの応用. 情報処理学会論文誌, 43(6), 1769-1777.
[^df-pn-plus]: Nagai, A. (2002). Df-pn algorithm for searching AND/OR trees and its applications. PhD thesis, Department of Information Science, University of Tokyo.
[^ghi]: Kishimoto, A., & Müller, M. (2004, July). A general solution to the graph history interaction problem. In AAAI (Vol. 4, pp. 644-649).
[^fix-depth-search]: 金子知適, 田中哲朗, 山口和紀, & 川合慧. (2010). 新規節点で固定深さの探索を行う df-pn の拡張. 情報処理学会論文誌, 51(11), 2040-2047.
[^tca-and-double-count]: Kishimoto, A. (2010, July). Dealing with infinite loops, underestimation, and overestimation of depth-first proof-number search. In Proceedings of the AAAI Conference on Artificial Intelligence (Vol. 24, No. 1, pp. 108-113).
[^deep-dfpn]: Song, Z., Iida, H., & van den Herik, J. (2016). Deep df-pn and Its Application to Connect6.
[^legal-check]: TadaoYamaoka. (2018-06-03). [王手生成の最大数 - TadaoYamaokaの開発日記](https://tadaoyamaoka.hatenablog.com/entry/2018/06/03/225012).
[^yane-5-million]: Yaneurao. (2020-12-25). [やねうら王公式からクリスマスプレゼントに詰将棋500万問を謹呈 | やねうら王 公式サイト](https://yaneuraou.yaneu.com/2020/12/25/christmas-present/)
