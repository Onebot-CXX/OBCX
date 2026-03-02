#include "exhentai_parser.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <memory>
#include <string>

namespace plugins {

namespace {

// RAII wrappers for libxml2 objects
struct XmlDocDeleter {
  void operator()(htmlDocPtr doc) const { xmlFreeDoc(doc); }
};
struct XmlXPathContextDeleter {
  void operator()(xmlXPathContextPtr ctx) const { xmlXPathFreeContext(ctx); }
};
struct XmlXPathObjectDeleter {
  void operator()(xmlXPathObjectPtr obj) const { xmlXPathFreeObject(obj); }
};
struct XmlCharDeleter {
  void operator()(xmlChar *s) const { xmlFree(s); }
};

using XmlDocPtr = std::unique_ptr<xmlDoc, XmlDocDeleter>;
using XmlXPathContextPtr =
    std::unique_ptr<xmlXPathContext, XmlXPathContextDeleter>;
using XmlXPathObjectPtr =
    std::unique_ptr<xmlXPathObject, XmlXPathObjectDeleter>;
using XmlCharPtr = std::unique_ptr<xmlChar, XmlCharDeleter>;

// Evaluate an XPath expression relative to a context node.
// Returns nullptr if expression fails or yields nothing.
XmlXPathObjectPtr eval_xpath(xmlXPathContextPtr ctx, xmlNodePtr node,
                             const char *expr) {
  ctx->node = node;
  auto *obj =
      xmlXPathEvalExpression(reinterpret_cast<const xmlChar *>(expr), ctx);
  return XmlXPathObjectPtr(obj);
}

// Extract the string value of an XPath expression relative to node.
std::string xpath_string(xmlXPathContextPtr ctx, xmlNodePtr node,
                         const char *expr) {
  auto obj = eval_xpath(ctx, node, expr);
  if (!obj) {
    return {};
  }
  XmlCharPtr val(xmlXPathCastToString(obj.get()));
  if (!val) {
    return {};
  }
  return std::string(reinterpret_cast<const char *>(val.get()));
}

} // namespace

std::vector<GalleryEntry> ExHentaiParser::parse(const std::string &html) {
  std::vector<GalleryEntry> entries;

  XmlDocPtr doc(htmlReadMemory(html.data(), static_cast<int>(html.size()),
                               nullptr, "UTF-8",
                               HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING));
  if (!doc) {
    return entries;
  }

  XmlXPathContextPtr ctx(xmlXPathNewContext(doc.get()));
  if (!ctx) {
    return entries;
  }

  // Select gallery rows: rows inside .itg table that contain a .gl3c cell
  static const char *kRowXPath =
      "//table[contains(@class,'itg')]//tr[.//td[contains(@class,'gl3c')]]";

  ctx->node = xmlDocGetRootElement(doc.get());
  auto rows_obj = eval_xpath(ctx.get(), ctx->node, kRowXPath);
  if (!rows_obj || rows_obj->type != XPATH_NODESET || !rows_obj->nodesetval ||
      rows_obj->nodesetval->nodeNr == 0) {
    return entries;
  }

  for (int i = 0; i < rows_obj->nodesetval->nodeNr; ++i) {
    xmlNodePtr row = rows_obj->nodesetval->nodeTab[i];
    GalleryEntry entry;

    entry.title =
        xpath_string(ctx.get(), row, "string(.//div[@class='glink'])");

    entry.url = xpath_string(ctx.get(), row,
                             "string(.//td[contains(@class,'gl3c')]//a/@href)");

    entry.thumbnail_url = xpath_string(
        ctx.get(), row, "string(.//div[contains(@class,'glthumb')]//img/@src)");

    // Tags
    auto tags_obj = eval_xpath(ctx.get(), row, ".//div[@class='gt']");
    if (tags_obj && tags_obj->type == XPATH_NODESET && tags_obj->nodesetval) {
      for (int j = 0; j < tags_obj->nodesetval->nodeNr; ++j) {
        xmlNodePtr tag_node = tags_obj->nodesetval->nodeTab[j];
        XmlCharPtr content(xmlNodeGetContent(tag_node));
        if (content) {
          std::string tag(reinterpret_cast<const char *>(content.get()));
          if (!tag.empty()) {
            entry.tags.push_back(std::move(tag));
          }
        }
      }
    }

    if (!entry.title.empty() || !entry.url.empty()) {
      entries.push_back(std::move(entry));
    }
  }

  return entries;
}

} // namespace plugins
