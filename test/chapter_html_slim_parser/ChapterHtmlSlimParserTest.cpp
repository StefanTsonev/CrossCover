#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

#include <Epub.h>

namespace {

TEST(ParagraphIndentTest, DoesNotInventIndentWithoutSourceCss) {
  GfxRenderer renderer;

  for (const bool extraParagraphSpacing : {false, true}) {
    ParsedText paragraph(extraParagraphSpacing);
    EXPECT_EQ(paragraph.resolveFirstLineIndent(true, renderer, 0), 0);
  }
}

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  Epub epub;
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{epub,  filepath, renderer, 0,  1.0f, false, false, 0, 480, 800,     false,
                               false, false,    0,        {}, true, "",    "",    0, {},  nullptr, &cssParser};
  std::array<ChapterHtmlSlimParser::StyleStackEntry, ChapterHtmlSlimParser::MAX_INLINE_STYLE_DEPTH> inlineStyles{};
  std::array<BlockStyle, ChapterHtmlSlimParser::MAX_BLOCK_STYLE_DEPTH> blockStyles{};

  void SetUp() override {
    parser.currentTextBlock = std::make_unique<ParsedText>(false);
    parser.inlineStyleBuf_ = inlineStyles.data();
    parser.blockStyleBuf_ = blockStyles.data();
    parser.blockStyleCount_ = 1;
  }

  BlockStyle parseParagraph(const char* className) {
    const XML_Char* attributes[] = {"class", className, nullptr};
    ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
    const BlockStyle style = parser.currentTextBlock->getBlockStyle();
    ChapterHtmlSlimParser::characterData(&parser, "Text", 4);
    ChapterHtmlSlimParser::endElement(&parser, "p");
    return style;
  }
};

TEST_F(ChapterHtmlSlimParserTest, BlockquoteParagraphsInheritItalicAndRestoreFollowingText) {
  cssParser.rulesBySelector_["blockquote"] =
      CssParser::parseInlineStyle("margin-left: 2em; margin-right: 1em; font-style: italic");
  ChapterHtmlSlimParser::startElement(&parser, "blockquote", nullptr);

  for (int i = 0; i < 2; ++i) {
    ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
    ChapterHtmlSlimParser::characterData(&parser, "Quoted", 6);
    // Closing the paragraph must flush its final word before changing styles.
    ChapterHtmlSlimParser::endElement(&parser, "p");
    ASSERT_EQ(parser.currentTextBlock->size(), 1u);
    EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::ITALIC);
    EXPECT_TRUE(parser.effectiveItalic);
    EXPECT_EQ(parser.inlineStyleCount_, 1u);
  }

  ChapterHtmlSlimParser::endElement(&parser, "blockquote");
  EXPECT_EQ(parser.inlineStyleCount_, 0u);
  EXPECT_FALSE(parser.effectiveItalic);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Following", 9);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);
}

TEST_F(ChapterHtmlSlimParserTest, NestedBlocksOverrideAndRestoreInheritedBoldAndItalic) {
  const XML_Char* parentAttributes[] = {"style", "font-weight: bold; font-style: italic", nullptr};
  const XML_Char* normalAttributes[] = {"style", "font-weight: normal; font-style: normal", nullptr};
  const XML_Char* italicAttributes[] = {"style", "font-style: italic", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "div", parentAttributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", normalAttributes);
  ChapterHtmlSlimParser::characterData(&parser, "Normal ", 7);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0), EpdFontFamily::REGULAR);

  ChapterHtmlSlimParser::startElement(&parser, "span", italicAttributes);
  ChapterHtmlSlimParser::characterData(&parser, "Italic", 6);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::ITALIC);
  ChapterHtmlSlimParser::characterData(&parser, "Normal", 6);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 3u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(2), EpdFontFamily::REGULAR);

  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Inherited", 9);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0),
            static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::ITALIC));
  ChapterHtmlSlimParser::endElement(&parser, "div");
  EXPECT_FALSE(parser.effectiveBold);
  EXPECT_FALSE(parser.effectiveItalic);
  EXPECT_EQ(parser.inlineStyleCount_, 0u);
}

TEST_F(ChapterHtmlSlimParserTest, NestedHeadingAndListPreserveBlockFontInheritance) {
  const XML_Char* attributes[] = {"style", "font-style: italic", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "blockquote", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "h2", nullptr);
  EXPECT_TRUE(parser.effectiveItalic);
  ChapterHtmlSlimParser::characterData(&parser, "Heading", 7);
  ChapterHtmlSlimParser::endElement(&parser, "h2");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0),
            static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::ITALIC));

  ChapterHtmlSlimParser::startElement(&parser, "ul", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Item", 4);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::ITALIC);
  ChapterHtmlSlimParser::endElement(&parser, "li");
  ChapterHtmlSlimParser::endElement(&parser, "ul");
  ChapterHtmlSlimParser::endElement(&parser, "blockquote");
  EXPECT_FALSE(parser.effectiveItalic);
  EXPECT_EQ(parser.inlineStyleCount_, 0u);
}

TEST_F(ChapterHtmlSlimParserTest, TableCellBlocksInheritOverrideAndRestoreFontStyles) {
  const XML_Char* parentAttributes[] = {"style", "font-weight: bold; font-style: italic", nullptr};
  const XML_Char* normalAttributes[] = {"style", "font-weight: normal; font-style: normal", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "table", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "tr", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "td", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "blockquote", parentAttributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Quoted", 6);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(0),
            static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::ITALIC));

  ChapterHtmlSlimParser::startElement(&parser, "p", normalAttributes);
  ChapterHtmlSlimParser::characterData(&parser, "Normal", 6);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(1), EpdFontFamily::REGULAR);
  EXPECT_TRUE(parser.effectiveBold);
  EXPECT_TRUE(parser.effectiveItalic);

  ChapterHtmlSlimParser::endElement(&parser, "blockquote");
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Following", 9);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ASSERT_EQ(parser.currentTextBlock->size(), 3u);
  EXPECT_EQ(parser.currentTextBlock->getWordStyleAt(2), EpdFontFamily::REGULAR);
  ChapterHtmlSlimParser::endElement(&parser, "td");
  EXPECT_EQ(parser.inlineStyleCount_, 0u);
  ChapterHtmlSlimParser::endElement(&parser, "tr");
  ChapterHtmlSlimParser::endElement(&parser, "table");
}

TEST_F(ChapterHtmlSlimParserTest, InheritsBodyTextIndentAndPreservesExplicitParagraphZero) {
  parser.cssParser->rulesBySelector_[".class-0"] =
      CssParser::parseInlineStyle("text-indent: 1.5em; text-align: justify");
  parser.cssParser->rulesBySelector_[".class_s4K-0"] = CssParser::parseInlineStyle("text-indent: 0");
  parser.cssParser->rulesBySelector_[".class_s4P-0"] = CssParser::parseInlineStyle("margin-top: 0; margin-bottom: 0");

  const XML_Char* bodyAttributes[] = {"class", "class-0", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "body", bodyAttributes);

  const BlockStyle openingParagraph = parseParagraph("class_s4K-0");
  EXPECT_TRUE(openingParagraph.textIndentDefined);
  EXPECT_EQ(openingParagraph.textIndent, 0);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 0);

  const BlockStyle followingParagraph = parseParagraph("class_s4P-0");
  EXPECT_TRUE(followingParagraph.textIndentDefined);
  EXPECT_EQ(followingParagraph.textIndent, 18);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 18);

  ChapterHtmlSlimParser::endElement(&parser, "body");
}

TEST_F(ChapterHtmlSlimParserTest, HtmlIndentFlowsThroughBodyAndBodyIndentOverridesIt) {
  parser.cssParser->rulesBySelector_[".html-indent"] = CssParser::parseInlineStyle("text-indent: 1em");
  parser.cssParser->rulesBySelector_[".body-indent"] = CssParser::parseInlineStyle("text-indent: 1.5em");
  parser.cssParser->rulesBySelector_[".plain"] = CssParser::parseInlineStyle("margin: 0");

  const XML_Char* htmlAttributes[] = {"class", "html-indent", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "html", htmlAttributes);
  EXPECT_EQ(parser.blockStyleBuf_[0].textIndent, 12);

  const XML_Char* bodyAttributes[] = {"class", "body-indent", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "body", bodyAttributes);

  const BlockStyle inheritedParagraph = parseParagraph("plain");
  EXPECT_TRUE(inheritedParagraph.textIndentDefined);
  EXPECT_EQ(inheritedParagraph.textIndent, 18);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 18);

  ChapterHtmlSlimParser::endElement(&parser, "body");
  ChapterHtmlSlimParser::endElement(&parser, "html");
}

TEST_F(ChapterHtmlSlimParserTest, InheritsTextIndentFromDivAndKeepsParagraphOverride) {
  parser.cssParser->rulesBySelector_[".ancestor-indent"] = CssParser::parseInlineStyle("text-indent: 1.5em");
  parser.cssParser->rulesBySelector_[".plain"] = CssParser::parseInlineStyle("margin: 0");
  parser.cssParser->rulesBySelector_[".zero"] = CssParser::parseInlineStyle("text-indent: 0");

  const XML_Char* divAttributes[] = {"class", "ancestor-indent", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "div", divAttributes);

  const BlockStyle inheritedParagraph = parseParagraph("plain");
  EXPECT_TRUE(inheritedParagraph.textIndentDefined);
  EXPECT_EQ(inheritedParagraph.textIndent, 18);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 18);

  const BlockStyle zeroParagraph = parseParagraph("zero");
  EXPECT_TRUE(zeroParagraph.textIndentDefined);
  EXPECT_EQ(zeroParagraph.textIndent, 0);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 0);

  ChapterHtmlSlimParser::endElement(&parser, "div");
}

TEST_F(ChapterHtmlSlimParserTest, BodyIndentRespectsSpacingAndForcedIndentSettings) {
  parser.cssParser->rulesBySelector_[".body-indent"] = CssParser::parseInlineStyle("text-indent: 1.5em");
  parser.cssParser->rulesBySelector_[".plain"] = CssParser::parseInlineStyle("margin: 0");
  parser.cssParser->rulesBySelector_[".zero"] = CssParser::parseInlineStyle("text-indent: 0");
  const XML_Char* bodyAttributes[] = {"class", "body-indent", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "body", bodyAttributes);

  parser.extraParagraphSpacing = true;
  parser.currentTextBlock = std::make_unique<ParsedText>(true, false);
  const BlockStyle spacedParagraph = parseParagraph("plain");
  EXPECT_EQ(spacedParagraph.textIndent, 18);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 0);

  parser.extraParagraphSpacing = false;
  const BlockStyle unspacedParagraph = parseParagraph("plain");
  EXPECT_EQ(unspacedParagraph.textIndent, 18);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 18);

  parser.forceParagraphIndents = true;
  const BlockStyle forcedZeroParagraph = parseParagraph("zero");
  EXPECT_EQ(forcedZeroParagraph.textIndent, renderer.getFontAscenderSize(0));
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), renderer.getFontAscenderSize(0));

  ChapterHtmlSlimParser::endElement(&parser, "body");
}

TEST_F(ChapterHtmlSlimParserTest, RootIndentIsIgnoredWhenEmbeddedStyleIsOff) {
  parser.embeddedStyle = false;
  parser.cssParser->rulesBySelector_[".body-indent"] = CssParser::parseInlineStyle("text-indent: 1.5em");
  parser.cssParser->rulesBySelector_[".plain"] = CssParser::parseInlineStyle("margin: 0");
  const XML_Char* bodyAttributes[] = {"class", "body-indent", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "body", bodyAttributes);

  const BlockStyle plainParagraph = parseParagraph("plain");
  EXPECT_FALSE(plainParagraph.textIndentDefined);
  EXPECT_EQ(parser.currentTextBlock->resolveFirstLineIndent(true, renderer, 0), 0);

  ChapterHtmlSlimParser::endElement(&parser, "body");
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");

  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  const auto style = parser.currentTextBlock->getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(parser.pendingFootnotes.size(), 1u);
  const FootnoteEntry& footnote = parser.pendingFootnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_NE(footnote.linkId, 0u);
  ASSERT_EQ(parser.currentTextBlock->wordBackgroundBlack.size(), 1u);
  const uint8_t wordLinkId =
      static_cast<uint8_t>((parser.currentTextBlock->wordBackgroundBlack.front() & TextBlock::WORD_FLAG_LINK_ID_MASK) >>
                           TextBlock::WORD_FLAG_LINK_ID_SHIFT);
  EXPECT_EQ(wordLinkId, footnote.linkId);
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, PreservesEmptyInlinePaddingBeforeDialogueText) {
  parser.cssParser->rulesBySelector_[".spacey"] = CssParser::parseInlineStyle("padding-left: 2em");
  ChapterHtmlSlimParser::characterData(&parser, "EERO:", 5);
  const XML_Char* attributes[] = {"class", "spacey", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, "Kappusiwai!", 11);
  parser.flushPartWordBuffer();

  ASSERT_EQ(parser.currentTextBlock->words.size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "EERO:");
  EXPECT_EQ(parser.currentTextBlock->words[1], "Kappusiwai!");
  ASSERT_EQ(parser.currentTextBlock->inlinePaddings.size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->inlinePaddings[0].wordIndex, 1u);
  EXPECT_EQ(parser.currentTextBlock->inlinePaddings[0].pixels, 24);
  EXPECT_TRUE(parser.currentTextBlock->wordContinues[1]);

  std::shared_ptr<TextBlock> renderedLine;
  ASSERT_TRUE(parser.currentTextBlock->layoutAndExtractLines(
      renderer, 0, 480,
      [&renderedLine](std::shared_ptr<TextBlock> line, uint32_t, uint32_t) { renderedLine = std::move(line); }));
  ASSERT_NE(renderedLine, nullptr);
  ASSERT_EQ(renderedLine->wordCount(), 2u);
  EXPECT_EQ(renderedLine->wordXpos(0), 0);
  EXPECT_EQ(renderedLine->wordXpos(1), 24);
}

TEST_F(ChapterHtmlSlimParserTest, UsesOptimizerImageDimensionsWithoutReadingTheCompressedImage) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 800;
  epub.optimizerImageHeight = 7;
  const XML_Char* attributes[] = {"src", "wide.jpg", nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);

  EXPECT_EQ(epub.streamReadCount, 0u);
  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_EQ(parser.currentPage->elements.size(), 1u);
  ASSERT_EQ(parser.currentPage->elements.front()->getTag(), TAG_PageImage);
  const auto& image = static_cast<const PageImage&>(*parser.currentPage->elements.front()).getImageBlock();
  EXPECT_EQ(image.getWidth(), 480);
  EXPECT_EQ(image.getHeight(), 4);
  EXPECT_FALSE(static_cast<const PageImage&>(*parser.currentPage->elements.front()).isInlineImage());
}

TEST_F(ChapterHtmlSlimParserTest, PlacesSmallImageInsideTextLine) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 16;
  epub.optimizerImageHeight = 16;
  const XML_Char* attributes[] = {"src", "icon.jpg", nullptr};

  ChapterHtmlSlimParser::characterData(&parser, "Before", 6);
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "img");
  ChapterHtmlSlimParser::characterData(&parser, "After", 5);
  parser.flushPartWordBuffer();
  parser.makePages();

  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_EQ(parser.currentPage->elements.size(), 2u);
  EXPECT_EQ(parser.currentPage->elements[0]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->getTag(), TAG_PageImage);
  const auto& line = static_cast<const PageLine&>(*parser.currentPage->elements[0]);
  EXPECT_EQ(line.getBlock()->wordCount(), 2u);
  const auto& image = static_cast<const PageImage&>(*parser.currentPage->elements[1]);
  EXPECT_TRUE(image.isInlineImage());
  EXPECT_EQ(image.yPos, parser.currentPage->elements[0]->yPos);
  EXPECT_EQ(image.getImageBlock().getWidth(), 16);
  EXPECT_TRUE(parser.pendingInlineImages.empty());
}

TEST_F(ChapterHtmlSlimParserTest, WrapsTextAfterInlineImageWithoutSplittingTheImage) {
  renderer.textAdvancePerChar = 4;
  parser.viewportWidth = 22;
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 16;
  epub.optimizerImageHeight = 16;
  const XML_Char* attributes[] = {"src", "icon.jpg", nullptr};

  ChapterHtmlSlimParser::characterData(&parser, "A", 1);
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "img");
  ChapterHtmlSlimParser::characterData(&parser, "B", 1);
  parser.flushPartWordBuffer();
  parser.makePages();

  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_EQ(parser.currentPage->elements.size(), 3u);
  EXPECT_EQ(parser.currentPage->elements[0]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->getTag(), TAG_PageImage);
  EXPECT_EQ(parser.currentPage->elements[2]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->xPos, 4);
  EXPECT_EQ(parser.currentPage->elements[0]->yPos, parser.currentPage->elements[1]->yPos);
  EXPECT_EQ(parser.currentPage->elements[2]->yPos, 16);
}

TEST_F(ChapterHtmlSlimParserTest, AlignsTextWithTallerInlineImage) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 20;
  epub.optimizerImageHeight = 32;
  const XML_Char* attributes[] = {"src", "icon.jpg", nullptr};

  ChapterHtmlSlimParser::characterData(&parser, "A", 1);
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "img");
  parser.makePages();

  ASSERT_EQ(parser.currentPage->elements.size(), 2u);
  EXPECT_EQ(parser.currentPage->elements[0]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->getTag(), TAG_PageImage);
  EXPECT_EQ(parser.currentPage->elements[0]->yPos, 16);
  EXPECT_EQ(parser.currentPage->elements[1]->yPos, 0);
  EXPECT_EQ(parser.currentPageNextY, 32);
}

TEST_F(ChapterHtmlSlimParserTest, HonorsExplicitBlockDisplayForSmallImage) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 16;
  epub.optimizerImageHeight = 16;
  const XML_Char* attributes[] = {"src", "icon.jpg", "style", "display: block", nullptr};

  ChapterHtmlSlimParser::characterData(&parser, "A", 1);
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);

  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_EQ(parser.currentPage->elements.size(), 2u);
  EXPECT_EQ(parser.currentPage->elements[0]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->getTag(), TAG_PageImage);
  EXPECT_GE(parser.currentPage->elements[1]->yPos, 16);
}

TEST_F(ChapterHtmlSlimParserTest, HonorsExplicitInlineDisplayForTallerImage) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 20;
  epub.optimizerImageHeight = 48;
  const XML_Char* attributes[] = {"src", "icon.jpg", "style", "display: inline", nullptr};

  ChapterHtmlSlimParser::characterData(&parser, "A", 1);
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "img");
  parser.makePages();

  ASSERT_EQ(parser.currentPage->elements.size(), 2u);
  EXPECT_EQ(parser.currentPage->elements[0]->getTag(), TAG_PageLine);
  EXPECT_EQ(parser.currentPage->elements[1]->getTag(), TAG_PageImage);
  EXPECT_EQ(parser.currentPage->elements[0]->yPos, 32);
  EXPECT_EQ(parser.currentPage->elements[1]->yPos, 0);
  EXPECT_EQ(parser.currentPageNextY, 48);
}

TEST_F(ChapterHtmlSlimParserTest, BoundsPendingImagesInAnIconOnlyParagraph) {
  epub.optimizerImageAvailable = true;
  epub.optimizerImageWidth = 1;
  epub.optimizerImageHeight = 1;
  const XML_Char* attributes[] = {"src", "icon.jpg", nullptr};

  for (int i = 0; i < 40; ++i) {
    ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
    ChapterHtmlSlimParser::endElement(&parser, "img");
    EXPECT_LT(parser.pendingInlineImages.size(), parser.MAX_PENDING_INLINE_IMAGES);
  }
  parser.makePages();

  ASSERT_NE(parser.currentPage, nullptr);
  EXPECT_TRUE(parser.pendingInlineImages.empty());
  EXPECT_EQ(parser.currentPage->elements.size(), 40u);
}

TEST_F(ChapterHtmlSlimParserTest, HiddenElementsSuppressContentAndResumeVisibleText) {
  for (const char* tag : {"p", "h1", "span", "div", "a", "table"}) {
    for (const char* value : {"hidden", "", "false"}) {
      const XML_Char* attributes[] = {"hidden", value, "style", "display: block", nullptr};
      ChapterHtmlSlimParser::startElement(&parser, tag, attributes);
      ChapterHtmlSlimParser::startElement(&parser, "span", nullptr);
      ChapterHtmlSlimParser::characterData(&parser, "HIDDEN ", 7);
      ChapterHtmlSlimParser::endElement(&parser, "span");
      ChapterHtmlSlimParser::endElement(&parser, tag);
      EXPECT_EQ(parser.currentTextBlock->size(), 0u);
      EXPECT_EQ(parser.partWordBufferIndex, 0);
    }
  }
  ChapterHtmlSlimParser::characterData(&parser, "Visible ", 8);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "Visible");
}

TEST_F(ChapterHtmlSlimParserTest, StablePageOffsetsCollapseClusteredWhitespace) {
  parser.trackReferenceCharacters = true;
  parser.currentTextBlock = std::make_unique<ParsedText>(false, false, false, false, false, 0, BlockStyle{}, true);

  constexpr char text[] = "  Alpha     Beta ";
  ChapterHtmlSlimParser::characterData(&parser, text, sizeof(text) - 1);
  parser.flushPartWordBuffer();

  ASSERT_EQ(parser.currentTextBlock->wordReferenceOffsets.size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->wordReferenceOffsets[0], 0u);
  EXPECT_EQ(parser.currentTextBlock->wordReferenceOffsets[1], 6u);
  EXPECT_EQ(parser.referenceTextOffset, 10u);
  EXPECT_TRUE(parser.referenceWhitespacePending);
}

TEST_F(ChapterHtmlSlimParserTest, StablePageOffsetsResumeAfterNestedExcludedMarkup) {
  parser.trackReferenceCharacters = true;
  parser.currentTextBlock = std::make_unique<ParsedText>(false, false, false, false, false, 0, BlockStyle{}, true);

  ChapterHtmlSlimParser::startElement(&parser, "html", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "head", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "style", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "p { display: block; }", 21);
  ChapterHtmlSlimParser::endElement(&parser, "style");
  ChapterHtmlSlimParser::endElement(&parser, "head");
  ChapterHtmlSlimParser::startElement(&parser, "body", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "svg", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "metadata", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "not book text", 13);
  ChapterHtmlSlimParser::endElement(&parser, "metadata");
  ChapterHtmlSlimParser::endElement(&parser, "svg");
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Visible text ", 13);
  parser.flushPartWordBuffer();

  EXPECT_EQ(parser.referenceExcludedUntilDepth, INT_MAX);
  EXPECT_EQ(parser.referenceTextOffset, 12u);
  ASSERT_EQ(parser.currentTextBlock->wordReferenceOffsets.size(), 2u);
  EXPECT_EQ(parser.currentTextBlock->wordReferenceOffsets[0], 0u);
  EXPECT_EQ(parser.currentTextBlock->wordReferenceOffsets[1], 8u);
}

TEST_F(ChapterHtmlSlimParserTest, HiddenImageDoesNotReadImageDataWithoutCss) {
  parser.cssParser = nullptr;
  const XML_Char* attributes[] = {"hidden", "", "src", "missing.jpg", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "img", attributes);
  ChapterHtmlSlimParser::endElement(&parser, "img");
  EXPECT_EQ(epub.streamReadCount, 0u);
  EXPECT_EQ(parser.currentPage, nullptr);
}

TEST_F(ChapterHtmlSlimParserTest, HiddenIdsDoNotBecomeAnchorsOrTocPageBreaks) {
  parser.tocAnchors.push_back("hidden-chapter");
  const XML_Char* idFirst[] = {"id", "hidden-chapter", "hidden", "hidden", nullptr};
  const XML_Char* hiddenFirst[] = {"hidden", "", "id", "hidden-chapter", nullptr};
  for (auto* attributes : {idFirst, hiddenFirst}) {
    ChapterHtmlSlimParser::startElement(&parser, "h1", attributes);
    ChapterHtmlSlimParser::characterData(&parser, "Hidden", 6);
    ChapterHtmlSlimParser::endElement(&parser, "h1");
    EXPECT_TRUE(parser.pendingAnchorId.empty());
    ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
    EXPECT_TRUE(parser.anchorData.empty());
    EXPECT_EQ(parser.completedPageCount, 0);
    ChapterHtmlSlimParser::endElement(&parser, "p");
  }
}

TEST_F(ChapterHtmlSlimParserTest, NumbersOrderedListsAndRestartsNestedCounters) {
  ChapterHtmlSlimParser::startElement(&parser, "ol", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "1.");

  ChapterHtmlSlimParser::startElement(&parser, "ol", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "1.");
  ChapterHtmlSlimParser::endElement(&parser, "li");
  ChapterHtmlSlimParser::endElement(&parser, "ol");

  ChapterHtmlSlimParser::endElement(&parser, "li");
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "2.");
}

TEST_F(ChapterHtmlSlimParserTest, HonorsOrderedListStartAndItemValue) {
  const XML_Char* listAttributes[] = {"start", "5", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "ol", listAttributes);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "5.");
  ChapterHtmlSlimParser::endElement(&parser, "li");

  const XML_Char* itemAttributes[] = {"value", "9", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "li", itemAttributes);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "9.");
  ChapterHtmlSlimParser::endElement(&parser, "li");

  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "10.");
}

TEST_F(ChapterHtmlSlimParserTest, SupportsNegativeOrderedListValues) {
  const XML_Char* listAttributes[] = {"start", "-2", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "ol", listAttributes);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "-2.");
  ChapterHtmlSlimParser::endElement(&parser, "li");

  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "-1.");
}

TEST_F(ChapterHtmlSlimParserTest, SupportsMarkerFreeListsAndContainerInsets) {
  const XML_Char* listAttributes[] = {"style", "list-style-type: none; margin-left: 10px; padding-left: 5px", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "ul", listAttributes);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);

  EXPECT_TRUE(parser.currentTextBlock->isEmpty());
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().leftInset(), 15);
}

TEST_F(ChapterHtmlSlimParserTest, HiddenNestedListDoesNotResetOuterCounter) {
  ChapterHtmlSlimParser::startElement(&parser, "ol", nullptr);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  EXPECT_EQ(parser.currentTextBlock->words[0], "1.");
  ChapterHtmlSlimParser::endElement(&parser, "li");

  const XML_Char* hidden[] = {"hidden", "", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "ul", hidden);
  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ChapterHtmlSlimParser::endElement(&parser, "li");
  ChapterHtmlSlimParser::endElement(&parser, "ul");

  ChapterHtmlSlimParser::startElement(&parser, "li", nullptr);
  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->words[0], "2.");
}

}  // namespace

namespace {
TEST_F(ChapterHtmlSlimParserTest, ScalableHeadingLevelsAndBodySizeCeiling) {
  renderer.scalableBaseSize = 12;
  const XML_Char* attrs[] = {nullptr};
  const uint8_t expected[] = {24, 18, 14, 12, 10, 8};
  for (int level = 1; level <= 6; ++level) {
    const char tag[] = {'h', static_cast<char>('0' + level), 0};
    ChapterHtmlSlimParser::startElement(&parser, tag, attrs);
    EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, expected[level - 1]);
    ChapterHtmlSlimParser::endElement(&parser, tag);
  }
  renderer.scalableBaseSize = 22;
  ChapterHtmlSlimParser::startElement(&parser, "h1", attrs);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 44);
}

TEST_F(ChapterHtmlSlimParserTest, BlockSizesInheritAndRestoreAcrossSiblings) {
  renderer.scalableBaseSize = 12;
  const XML_Char* parent[] = {"style", "font-size: 150%", nullptr};
  const XML_Char* child[] = {"style", "font-size: 0.5em", nullptr};
  const XML_Char* plain[] = {nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "div", parent);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
  ChapterHtmlSlimParser::startElement(&parser, "p", child);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 9);
  ChapterHtmlSlimParser::characterData(&parser, "Small", 5);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
  ChapterHtmlSlimParser::characterData(&parser, "Parent", 6);
  ChapterHtmlSlimParser::startElement(&parser, "p", plain);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ChapterHtmlSlimParser::endElement(&parser, "div");
  ChapterHtmlSlimParser::startElement(&parser, "p", plain);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 12);
}

TEST_F(ChapterHtmlSlimParserTest, TextAfterTableKeepsParentFontSize) {
  renderer.scalableBaseSize = 12;
  const XML_Char* parent[] = {"style", "font-size: 150%", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "div", parent);
  ChapterHtmlSlimParser::startElement(&parser, "table", nullptr);
  ChapterHtmlSlimParser::endElement(&parser, "table");
  ASSERT_NE(parser.currentTextBlock, nullptr);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontScale, 384);
  ChapterHtmlSlimParser::characterData(&parser, "Parent", 6);
  ChapterHtmlSlimParser::endElement(&parser, "div");
  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_FALSE(parser.currentPage->elements.empty());
  const auto& line = static_cast<const PageLine&>(*parser.currentPage->elements.back());
  EXPECT_EQ(line.getBlock()->getBlockStyle().fontSize, 18);
  EXPECT_EQ(line.getBlock()->getBlockStyle().lineHeight, 36);
}

TEST_F(ChapterHtmlSlimParserTest, RootRelativeSizesAndAbsoluteSizesRespectReaderZoom) {
  renderer.scalableBaseSize = 16;
  const XML_Char* html[] = {"style", "font-size: 125%", nullptr};
  const XML_Char* body[] = {"style", "font-size: 150%", nullptr};
  const XML_Char* rem[] = {"style", "font-size: 1rem", nullptr};
  const XML_Char* points[] = {"style", "font-size: 12pt", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "html", html);
  ChapterHtmlSlimParser::startElement(&parser, "body", body);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 30);
  ChapterHtmlSlimParser::startElement(&parser, "p", rem);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 20);
  ChapterHtmlSlimParser::endElement(&parser, "p");
  ChapterHtmlSlimParser::startElement(&parser, "p", points);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 16);
}

TEST_F(ChapterHtmlSlimParserTest, PublisherSizeOverridesHeadingDefaultAndInlineSizesStayUniform) {
  renderer.scalableBaseSize = 12;
  const XML_Char* heading[] = {"style", "font-size: 150%", nullptr};
  const XML_Char* span[] = {"style", "font-size: 300%", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "h1", heading);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
  ChapterHtmlSlimParser::startElement(&parser, "span", span);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
}

TEST_F(ChapterHtmlSlimParserTest, BitmapAndLightModesKeepUniformSize) {
  const XML_Char* heading[] = {"style", "font-size: 200%", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "h1", heading);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 0);
  ChapterHtmlSlimParser::endElement(&parser, "h1");
  renderer.scalableBaseSize = 12;
  parser.renderMode = EpubRenderMode::Light;
  ChapterHtmlSlimParser::startElement(&parser, "h1", heading);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 0);
}

TEST_F(ChapterHtmlSlimParserTest, DisabledPublisherStylingKeepsSemanticHeadings) {
  renderer.scalableBaseSize = 12;
  parser.embeddedStyle = false;
  const XML_Char* heading[] = {"style", "font-size: 300%", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "h2", heading);
  EXPECT_EQ(parser.currentTextBlock->getBlockStyle().fontSize, 18);
}

TEST_F(ChapterHtmlSlimParserTest, LargerHeadingsWrapAndReserveTheirActualHeight) {
  renderer.scalableBaseSize = 12;
  renderer.textAdvancePerChar = 4;
  parser.viewportWidth = 100;
  const XML_Char* attrs[] = {nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "h1", attrs);
  const char* text = "four four four four four four four four";
  ChapterHtmlSlimParser::characterData(&parser, text, std::strlen(text));
  ChapterHtmlSlimParser::endElement(&parser, "h1");
  ASSERT_NE(parser.currentPage, nullptr);
  ASSERT_GT(parser.currentPage->elements.size(), 2u);
  int previousY = -48;
  for (const auto& element : parser.currentPage->elements) {
    ASSERT_EQ(element->getTag(), TAG_PageLine);
    const auto& line = static_cast<const PageLine&>(*element);
    EXPECT_EQ(line.getBlock()->getBlockStyle().fontSize, 24);
    EXPECT_EQ(line.getBlock()->getBlockStyle().lineHeight, 48);
    EXPECT_GE(line.yPos - previousY, 48);
    previousY = line.yPos;
  }
}

TEST(CssFontSizeTest, StrictValuesAndCascade) {
  for (const char* value : {"12badpx", "12foorem", "10vw", "12", "nanem", "-2em", "0px"}) {
    EXPECT_FALSE(CssParser::parseInlineStyle(std::string("font-size: ") + value).hasFontSize()) << value;
  }
  for (const char* value : {"150%", "1.25em", "1rem", "12pt", "16px", "larger", "small", "inherit"}) {
    EXPECT_TRUE(CssParser::parseInlineStyle(std::string("font-size: ") + value).hasFontSize()) << value;
  }
  const auto style = CssParser::parseInlineStyle("font-size: 150%; font-size: nonsense");
  EXPECT_TRUE(style.hasFontSize());
  EXPECT_FLOAT_EQ(style.fontSize.value, 150);
}
}  // namespace
