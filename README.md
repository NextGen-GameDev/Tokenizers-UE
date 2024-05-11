<div id="top"></div>

<div align="center">
  <a href="https://github.com/P1ayer-1/Tokenizers-UE5">
    <img src="Resources/Icon128.png" alt="Logo" width="128" height="128">
  </a>
<h3 align="center">Tokenizers</h3>
  <p align="center">HuggingFace Tokenizers in UE</p>

[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![MIT License][license-shield]][license-url] 
</div>


<div align="center">

[![](https://dcbadge.limes.pink/api/server/AWWECeRcyX?theme=default-inverted)](https://discord.gg/AWWECeRcyX)
</div>

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;


<details open>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-plugin">About the Plugin</a>
    </li>
    <li><a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#setup">Setup</a></li>
      </ul>
    </li>
    <li><a href="#guides-and-tutorials">Guides and Tutorials</a>
      <ul>
        <li><a href="#youtube-tutorials">YouTube Tutorials</a></li>
        <li><a href="#wiki">Wiki</a></li>
      </ul>
    </li>
    <li>
      <a href="#contributing">Contributing</a>
    </li>
    <li>
      <a href="#license">Credits and Licenses</a>
    </li>
  </ol>
</details>

<div id="about-the-plugin"></div>

## About the Plugin

Tokenizers is an _experimental_ plugin which enables developers to use tokenizers inside Unreal Engine's environment.

With this plugin you can:
* Initialize tokenizers from JSON blob or file configuration
* Encode and Decode text
* Use every feature in both C++ and Blueprints
<p align="right">(<a href="#top">back to top</a>)</p>

<div id="getting-started"></div>

## Getting Started

To use this plugin, you'll need the C static library from [Tokenizers-cpp](https://github.com/mlc-ai/tokenizers-cpp.git). You can either download it directly from the [Releases page](https://github.com/P1ayer-1/Tokenizers-UE5/releases) of this repository or compile it yourself from the [Tokenizers-cpp source](https://github.com/mlc-ai/tokenizers-cpp.git).

<div id="prerequisites"></div>

### Prerequisites

* OS: Windows - 64 bit
* UE: version 5.0 - 5.3

<div id="setup"></div>

### Setup

1. In your Unreal Engine project, create a `Plugins` folder if it doesn't already exist.
2. Navigate to the [Releases page](https://github.com/P1ayer-1/Tokenizers-UE5/releases).
3. Download the source code for the release you want to use.
4. Extract the downloaded source code into the `Plugins` directory.
5. Navigate to `Plugins/Tokenizers-UE5/Source/ThirdParty/TokenizersLibrary/Win64`.
6. From the same release page, download `tokenizers_c.lib` and place it inside the `Win64` folder.
7. Delete the placeholder file named `PLACE STATIC LIB HERE` from the `Win64` folder.

<p align="right">(<a href="#top">back to top</a>)</p>
<div id="guides-and-tutorials"></div>

## Guides and Tutorials

<div id="youtube-tutorials"></div>

### YouTube Tutorials

- [Setup and Basic Usage](https://youtu.be/dvGWUh4SPBY)
- [Working with Meta's Llama](https://youtu.be/0YI2O5uSuFw)

<div id="wiki"></div>

### Wiki
- https://github.com/gpt-3d/pytuech/wiki

<p align="right">(<a href="#top">back to top</a>)</p>

<div id="contributing"></div>

## Contributing
Want to contribute to? Awesome! Check out the [contributing guidelines](https://github.com/P1ayer-1/Tokenizers-UE5/blob/main/CONTRIBUTING.md) to get involved. Contributors are encouraged join to the [community Discord server](https://discord.gg/AWWECeRcyX).

<p align="right">(<a href="#top">back to top</a>)</p>
<div id="license"></div>

## Credits and Licenses

This project is licensed under the MIT License, except for specific files noted below. See the `LICENSE` file for more information.

- **Tokenizers-cpp**:
    - Source: [Tokenizers-cpp GitHub](https://github.com/mlc-ai/tokenizers-cpp)
    - License for `tokenizers_c.h`: Apache License 2.0

This project is based on [MLC-AI's C/C++ implementation](https://github.com/mlc-ai/tokenizers-cpp) of [HuggingFace's Tokenizers library](https://github.com/huggingface/tokenizers).
<p align="right">(<a href="#top">back to top</a>)</p>

[contributors-shield]: https://img.shields.io/github/contributors/P1ayer-1/Tokenizers-UE5.svg?style=for-the-badge
[contributors-url]: https://github.com/P1ayer-1/Tokenizers-UE5/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/P1ayer-1/Tokenizers-UE5.svg?style=for-the-badge
[forks-url]: https://github.com/P1ayer-1/Tokenizers-UE5/network/members
[stars-shield]: https://img.shields.io/github/stars/P1ayer-1/Tokenizers-UE5.svg?style=for-the-badge
[stars-url]: https://github.com/P1ayer-1/Tokenizers-UE5/stargazers
[issues-shield]: https://img.shields.io/github/issues/P1ayer-1/Tokenizers-UE5.svg?style=for-the-badge
[issues-url]: https://github.com/P1ayer-1/Tokenizers-UE5/issues
[license-shield]: https://img.shields.io/github/license/P1ayer-1/Tokenizers-UE5.svg?style=for-the-badge
[license-url]: https://github.com/P1ayer-1/Tokenizers-UE5/blob/master/LICENSE
