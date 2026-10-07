#include <OpdsParser.h>
#include <gtest/gtest.h>

namespace {

constexpr char kMultiAuthorFeed[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Book Title</title>
    <id>urn:booklore:book:90</id>
    <author><name>Main Author Name</name></author>
    <author><name>Translator Name</name></author>
    <link href="/api/v1/opds/90/download?fileId=691" rel="http://opds-spec.org/acquisition" type="application/epub+zip" title="EPUB"/>
  </entry>
</feed>)";

// Sanitized from a live Mayberry /opds/branches feed.
constexpr char kSummaryCountFeed[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opds="http://opds-spec.org/2010/catalog">
  <id>urn:example:branches</id>
  <title>Branches</title>
  <entry>
    <id>urn:example:branch:a</id>
    <title>Branch A</title>
    <summary type="text">12713 books</summary>
    <link rel="subsection" href="/opds/branch/a" type="application/atom+xml;profile=opds-catalog;kind=acquisition"></link>
  </entry>
  <entry>
    <id>urn:example:branch:b</id>
    <title>Branch B</title>
    <summary type="text">1 books</summary>
    <link rel="subsection" href="/opds/branch/b" type="application/atom+xml;profile=opds-catalog;kind=acquisition"></link>
  </entry>
  <entry>
    <id>urn:example:releases</id>
    <title>New Releases</title>
    <link rel="subsection" href="/opds/releases" type="application/atom+xml;profile=opds-catalog;kind=acquisition"></link>
  </entry>
  <entry>
    <id>urn:example:descr</id>
    <title>Described</title>
    <summary type="text">3 friends set out on a journey</summary>
    <link rel="subsection" href="/opds/descr" type="application/atom+xml;profile=opds-catalog;kind=navigation"></link>
  </entry>
</feed>)";

constexpr char kThrCountFeed[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:thr="http://purl.org/syndication/thread/1.0">
  <entry>
    <title>Fiction</title>
    <summary>99 books</summary>
    <link rel="subsection" href="/fiction" type="application/atom+xml;profile=opds-catalog" thr:count="42"/>
  </entry>
  <entry>
    <title>A Book</title>
    <summary>5 stars</summary>
    <link href="/b.epub" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
</feed>)";

}  // namespace

TEST(OpdsParserTest, MultipleAuthorsKeepFirstAuthor) {
  OpdsEntry entries[MAX_OPDS_FEED_ENTRIES];
  OpdsParser parser(entries);

  ASSERT_TRUE(parser.parse(kMultiAuthorFeed, sizeof(kMultiAuthorFeed) - 1));
  ASSERT_EQ(parser.getEntryCount(), 1u);
  ASSERT_NE(parser.getEntry(0), nullptr);
  EXPECT_EQ(parser.getEntry(0)->author, "Main Author Name");
}

TEST(OpdsParserTest, NavigationCountFromSummary) {
  OpdsEntry entries[MAX_OPDS_FEED_ENTRIES];
  OpdsParser parser(entries);

  ASSERT_TRUE(parser.parse(kSummaryCountFeed, sizeof(kSummaryCountFeed) - 1));
  ASSERT_EQ(parser.getEntryCount(), 4u);
  EXPECT_EQ(parser.getEntry(0)->count, 12713);
  EXPECT_EQ(parser.getEntry(1)->count, 1);
  EXPECT_EQ(parser.getEntry(2)->count, -1);
  EXPECT_EQ(parser.getEntry(3)->count, -1);
}

TEST(OpdsParserTest, NavigationCountFromIndentedSummary) {
  constexpr char feed[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <id>urn:example:indented</id>
    <title>Indented</title>
    <summary type="text">
                        12713 books
                    </summary>
    <link rel="subsection" href="/opds/a" type="application/atom+xml;profile=opds-catalog;kind=acquisition"></link>
  </entry>
  <entry>
    <id>urn:example:sentence</id>
    <title>Sentence</title>
    <summary type="text">
        3 books      that were picked by the staff this week
    </summary>
    <link rel="subsection" href="/opds/b" type="application/atom+xml;profile=opds-catalog;kind=acquisition"></link>
  </entry>
</feed>)";
  OpdsEntry entries[MAX_OPDS_FEED_ENTRIES];
  OpdsParser parser(entries);

  ASSERT_TRUE(parser.parse(feed, sizeof(feed) - 1));
  ASSERT_EQ(parser.getEntryCount(), 2u);
  EXPECT_EQ(parser.getEntry(0)->count, 12713);
  EXPECT_EQ(parser.getEntry(1)->count, -1);
}

TEST(OpdsParserTest, ThrCountWinsAndBooksHaveNoCount) {
  OpdsEntry entries[MAX_OPDS_FEED_ENTRIES];
  OpdsParser parser(entries);

  ASSERT_TRUE(parser.parse(kThrCountFeed, sizeof(kThrCountFeed) - 1));
  ASSERT_EQ(parser.getEntryCount(), 2u);
  EXPECT_EQ(parser.getEntry(0)->type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(parser.getEntry(0)->count, 42);
  EXPECT_EQ(parser.getEntry(1)->type, OpdsEntryType::BOOK);
  EXPECT_EQ(parser.getEntry(1)->count, -1);
}

TEST(OpdsParserTest, CountBoundsAndUnrelatedSummaries) {
  const char* values[] = {"0", "2147483647", "2147483648", "-1", "12x", ""};
  const int32_t expected[] = {0, INT32_MAX, -1, -1, -1, -1};
  for (size_t i = 0; i < 6; ++i) {
    const std::string feed =
        "<feed xmlns:t='http://purl.org/syndication/thread/1.0'><entry><title>Category</title>"
        "<link href='/a' type='application/atom+xml' t:count='" +
        std::string(values[i]) + "'/><summary>1984 classics</summary></entry></feed>";
    OpdsEntry entries[1];
    OpdsParser parser(entries, 1);
    // Exercise text split at arbitrary network boundaries as well as parser reuse.
    for (const unsigned char ch : feed) parser.write(ch);
    parser.flush();
    ASSERT_FALSE(parser.error());
    ASSERT_EQ(parser.getEntryCount(), 1u);
    EXPECT_EQ(entries[0].count, expected[i]);
    constexpr char emptyFeed[] =
        "<feed><entry><title>Empty</title><link href='/b' type='application/atom+xml'/></entry></feed>";
    ASSERT_TRUE(parser.parse(emptyFeed, sizeof(emptyFeed) - 1));
    EXPECT_EQ(entries[0].count, -1);
  }
}
