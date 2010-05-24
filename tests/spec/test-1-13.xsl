<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:foo="http://example.com/foo" xmlns:jcs="http://xml.juniper.net/jcs" xmlns:a="http://example.com/1" xmlns="http://example.com/global" xmlns:b="http://example.com/2" version="1.0" exclude-result-prefixes="foo" extension-element-prefixes="jcs">
  <xsl:template xmlns:c="http://example.com/3" match="/">
    <top xmlns:a="http://example.com/4">
      <xsl:apply-templates select="commit-script-input/configuration"/>
    </top>
  </xsl:template>
</xsl:stylesheet>
