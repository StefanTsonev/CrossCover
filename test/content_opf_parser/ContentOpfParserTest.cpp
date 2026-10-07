#include <gtest/gtest.h>

#include <string>

#include "ContentOpfParser.h"
#include "Epub/BookMetadataCache.h"

namespace {

// The parser retains references to both paths, so they must outlive each instance.
const std::string kEmptyPath;
const std::string kMissingCachePath = "/missing-cache";
const std::string kContentPath = "OPS/";

void parse(ContentOpfParser& parser, const std::string& xml) {
  ASSERT_TRUE(parser.setup());
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
}

}  // namespace

TEST(ContentOpfParserMetadata, EntityCallbackDoesNotSplitOneAuthor) {
  const std::string xml =
      R"(<package xmlns:dc="http://purl.org/dc/elements/1.1/"><metadata><dc:creator>&#201;mile Zola</dc:creator></metadata></package>)";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.author, "Émile Zola");
}

TEST(ContentOpfParserMetadata, ClampsOversizedMetadataTextInsteadOfGrowingUnbounded) {
  const std::string hugeTitle(64 * 1024, 'A');
  const std::string xml = "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata><dc:title>" + hugeTitle +
                          " tail</dc:title></metadata></package>";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title.size(), 512u);
  EXPECT_EQ(parser.title[0], 'A');
}

TEST(ContentOpfParserMetadata, ClampDoesNotSplitUtf8Codepoints) {
  const std::string prefix(511, 'A');
  const std::string xml = "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata><dc:title>" + prefix +
                          "\xC3\xA9&amp;tail</dc:title></metadata></package>";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title, prefix);
}

TEST(ContentOpfParserMetadata, ClampNeverOvershootsAtAMultiCreatorSeparatorBoundary) {
  // First creator fills the field to exactly one byte under the cap, so the
  // second creator's leading ", " separator is the thing that would push the
  // total past 512 if the clamp checked only the next character.
  const std::string firstAuthor(511, 'A');
  const std::string xml = "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata><dc:creator>" +
                          firstAuthor + "</dc:creator><dc:creator>B</dc:creator></metadata></package>";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_LE(parser.author.size(), 512u);
  EXPECT_EQ(parser.author, firstAuthor);
}

TEST(ContentOpfParserMetadata, SeparatesCreatorElementsAndCollapsesXmlWhitespace) {
  const std::string xml = R"(<package xmlns:dc="http://purl.org/dc/elements/1.1/"><metadata>
    <dc:title>  The
   Left Hand   of Darkness  </dc:title>
    <dc:creator> Ursula   K. Le Guin </dc:creator>
    <dc:creator>
Octavia E. Butler
</dc:creator>
  </metadata></package>)";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);

  parse(parser, xml);

  EXPECT_EQ(parser.title, "The Left Hand of Darkness");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin, Octavia E. Butler");
}

TEST(ContentOpfParserMetadata, ReadsCalibreSeriesAndFirstSubject) {
  const std::string xml = R"(<package xmlns:dc="http://purl.org/dc/elements/1.1/"><metadata>
    <meta content="Earthsea" name="calibre:series"/>
    <meta content="2.5" name="calibre:series_index"/>
    <dc:subject> Fantasy &amp; Adventure </dc:subject>
    <dc:subject>Young adult</dc:subject>
  </metadata></package>)";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);
  parse(parser, xml);
  EXPECT_EQ(parser.series, "Earthsea");
  EXPECT_EQ(parser.seriesIndex, "2.5");
  EXPECT_EQ(parser.subject, "Fantasy & Adventure");
}

TEST(ContentOpfParserMetadata, DistinguishesEpubThreeSeriesFromSets) {
  const std::string xml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="collection">Earthsea</meta>
    <meta refines="#collection" property="collection-type">series</meta>
    <meta refines="#collection" property="group-position"> 3 </meta>
  </metadata></package>)";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);
  parse(parser, xml);
  EXPECT_EQ(parser.series, "Earthsea");
  EXPECT_EQ(parser.seriesIndex, "3");

  const std::string setXml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="collection">Boxed books</meta>
    <meta refines="#collection" property="collection-type">set</meta>
    <meta refines="#collection" property="group-position">1</meta>
  </metadata></package>)";
  ContentOpfParser setParser(kEmptyPath, kEmptyPath, setXml.size(), nullptr);
  parse(setParser, setXml);
  EXPECT_TRUE(setParser.series.empty());
  EXPECT_TRUE(setParser.seriesIndex.empty());

  const std::string mixedXml = R"(<package><metadata>
    <meta property="belongs-to-collection" id="set">Boxed books</meta>
    <meta refines="#set" property="collection-type">set</meta>
    <meta property="belongs-to-collection" id="series">Earthsea</meta>
    <meta refines="#series" property="collection-type">series</meta>
    <meta refines="#series" property="group-position">2</meta>
  </metadata></package>)";
  ContentOpfParser mixedParser(kEmptyPath, kEmptyPath, mixedXml.size(), nullptr);
  parse(mixedParser, mixedXml);
  EXPECT_EQ(mixedParser.series, "Earthsea");
  EXPECT_EQ(mixedParser.seriesIndex, "2");
}

TEST(ContentOpfParserMetadata, StopsBeforeManifestWithoutOpeningTemporaryStorage) {
  const std::string xml = R"(<package xmlns:dc="http://purl.org/dc/elements/1.1/"><metadata>
    <dc:title>A Wizard of Earthsea</dc:title>
    <dc:creator>Ursula K. Le Guin</dc:creator>
    <dc:language>en</dc:language>
  </metadata><manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
  </package>)";
  Storage = {};
  ContentOpfParser parser(kMissingCachePath, kContentPath, xml.size(), nullptr, /*collectCssFiles=*/true,
                          /*metadataOnly=*/true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(parser.title, "A Wizard of Earthsea");
  EXPECT_EQ(parser.author, "Ursula K. Le Guin");
  EXPECT_EQ(parser.language, "en");
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserMetadata, NeverEntersManifestWhenMetadataElementIsMissing) {
  const std::string xml =
      R"(<package><manifest><item id="chapter" href="chapter.xhtml"/></manifest><spine/></package>)";
  Storage = {};
  ContentOpfParser parser(kMissingCachePath, kContentPath, xml.size(), nullptr, /*collectCssFiles=*/true,
                          /*metadataOnly=*/true);

  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserNamespaces, RecognizesOptimizerPrefixesAndAlternateDublinCorePrefix) {
  const std::string xml = R"(<ns0:package xmlns:ns0="http://www.idpf.org/2007/opf"
      xmlns:d="http://purl.org/dc/elements/1.1/">
    <ns0:metadata><d:title>Sample</d:title><d:creator>Author</d:creator>
      <d:language>en</d:language><d:subject>Fiction</d:subject>
    </ns0:metadata>
    <ns0:manifest>
      <ns0:item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>
      <ns0:item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
      <ns0:item id="css" href="style.css" media-type="text/css"/>
    </ns0:manifest>
    <ns0:spine><ns0:itemref idref="chapter"/></ns0:spine>
    <ns0:guide><ns0:reference type="start" href="chapter.xhtml"/></ns0:guide>
  </ns0:package>)";
  Storage = {};
  const std::string cachePath = "/cache";
  const std::string basePath = "OPS/";
  BookMetadataCache cache;
  ContentOpfParser parser(cachePath, basePath, xml.size(), &cache);
  parse(parser, xml);
  ASSERT_EQ(cache.spine.size(), 1u);
  EXPECT_EQ(cache.spine[0], "OPS/chapter.xhtml");
  EXPECT_EQ(parser.title, "Sample");
  EXPECT_EQ(parser.author, "Author");
  EXPECT_EQ(parser.language, "en");
  EXPECT_EQ(parser.subject, "Fiction");
  EXPECT_EQ(parser.tocNcxPath, "OPS/toc.ncx");
  ASSERT_EQ(parser.cssFiles.size(), 1u);
  EXPECT_EQ(parser.cssFiles[0], "OPS/style.css");
  EXPECT_EQ(parser.textReferenceHref, "OPS/chapter.xhtml");
  EXPECT_EQ(Storage.writeOpens, 1);
  EXPECT_EQ(Storage.readOpens, 2);
}

TEST(ContentOpfParserNamespaces, MetadataOnlyStopsBeforePrefixedManifest) {
  const std::string xml = R"(<ns0:package xmlns:ns0="http://www.idpf.org/2007/opf"
      xmlns:dc="http://purl.org/dc/elements/1.1/">
    <ns0:metadata><dc:title>Sample</dc:title></ns0:metadata>
    <ns0:manifest><ns0:item id="chapter" href="chapter.xhtml"/></ns0:manifest>
  </ns0:package>)";
  Storage = {};
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr, true, true);
  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(parser.title, "Sample");
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserNamespaces, MetadataOnlySkipsPrefixedManifestWithoutMetadata) {
  const std::string xml = R"(<ns0:package xmlns:ns0="http://www.idpf.org/2007/opf">
    <ns0:manifest><ns0:item id="chapter" href="chapter.xhtml"/></ns0:manifest>
  </ns0:package>)";
  Storage = {};
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr, true, true);
  ASSERT_TRUE(parser.setup());
  EXPECT_LT(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()), xml.size());
  EXPECT_EQ(Storage.writeOpens, 0);
  EXPECT_EQ(Storage.readOpens, 0);
}

TEST(ContentOpfParserNamespaces, IgnoresUnrelatedNamespacesWithMatchingLocalNames) {
  const std::string xml = R"(<package xmlns="http://www.idpf.org/2007/opf"
      xmlns:other="urn:other" xmlns:dc="http://purl.org/dc/elements/1.1/">
    <metadata><other:title>Wrong title</other:title><dc:title>Right title</dc:title></metadata>
    <other:manifest><other:item id="wrong" href="wrong.css" media-type="text/css"/></other:manifest>
    <manifest><item id="right" href="right.css" media-type="text/css"/></manifest>
  </package>)";
  ContentOpfParser parser(kEmptyPath, kEmptyPath, xml.size(), nullptr);
  parse(parser, xml);
  EXPECT_EQ(parser.title, "Right title");
  ASSERT_EQ(parser.cssFiles.size(), 1u);
  EXPECT_EQ(parser.cssFiles[0], "right.css");
}
