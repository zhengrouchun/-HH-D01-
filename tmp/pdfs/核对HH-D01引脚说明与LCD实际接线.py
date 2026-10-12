# 导入 PDF 读取和渲染库；库安装在工程临时目录。
import pymupdf
# 导入路径工具，用于定位资料及保存图片。
from pathlib import Path
# 明确列出需要核对的四份原始资料。
files = [Path('src/application/ws63/ws63_liteos_application/HH D01可以使用的引脚及引脚不能用的解释pdf.pdf'), Path('D:/ws/HH D01原理图及相关资料/HH-D01 星闪开发板规格说明书-20241212-V03.pdf'), Path('D:/ws/HH D01原理图及相关资料/HH-D01 开发板原理图.pdf'), Path('D:/ws/lcd/转接板资料/SPI/3.5寸SPI接口模块35002数据手册.pdf')]
# 把检查图片保存在工程临时目录。
out = Path('tmp/pdfs/HH-D01引脚说明核对')
# 创建缺少的目录，已有目录则继续使用。
out.mkdir(parents=True, exist_ok=True)
# 给每份资料编号，便于对应图片文件。
for index, path in enumerate(files):
    # 打开当前 PDF。
    document = pymupdf.open(path)
    # 输出资料路径、页数和是否需要密码。
    print('FILE', str(path), 'PAGES', len(document), 'PASSWORD', document.needs_pass)
    # 用户说明和原理图检查全部页面；两份手册检查供电和接口相关页面。
    pages = range(len(document)) if index in [0, 2] else ([4, 5, 7] if index == 1 else [2, 4, 5])
    # 逐页读取并渲染。
    for number in pages:
        # 输出从一开始计数的页号及该页完整文字。
        print('PAGE', number + 1, document[number].get_text())
        # 将页面放大到原尺寸的 1.5 倍并保存 PNG，用于检查表格与连线。
        document[number].get_pixmap(matrix=pymupdf.Matrix(1.5, 1.5)).save(out / f'doc{index}_page{number + 1}.png')
