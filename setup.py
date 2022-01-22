from pathlib import Path
from setuptools import setup


readme = (Path(__file__).parent / 'README.rst').read_text()


setup(
    name='mbgui',
    version='0.0.1',
    description='Maildir GUI based on mblaze',
    long_description=readme,
    long_description_content_type='text/x-rst',
    url='https://github.com/bozokopic/mbgui',
    packages=['mbgui'],
    package_data={'mbgui': ['icons/*.png']},
    license='GPLv3',
    classifiers=[
        'Programming Language :: Python :: 3',
        'License :: OSI Approved :: GNU General Public License v3 (GPLv3)'],
    entry_points={'console_scripts': ['mbgui = mbgui.main:main']})
