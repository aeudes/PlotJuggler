#include "custom_function.h"

#include <limits>
#include <QFile>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QJSValueIterator>

CustomFunction::CustomFunction(const std::string &linkedPlot,
                               const SnippetData &snippet):
    CustomFunction(linkedPlot,
                   snippet.name.toStdString(),
                   snippet.globalVars,
                   snippet.equation )
{
}

QStringList CustomFunction::getChannelsFromFuntion(const QString& function)
{
    QStringList output;
    int offset = 0;
    while(true)
    {
        int pos1 = function.indexOf("$$", offset);
        if(pos1 == -1){
            break;
        }

        int pos2 = function.indexOf("$$", pos1+2);
        if(pos2 == -1)
        {
            return {};
        }
        output.push_back( function.mid(pos1+2, pos2-pos1-2) );
        offset = pos2+2;
    }
    return output;
}

CustomFunction::CustomFunction(const std::string &linkedPlot,
                               const std::string &plotName,
                               const QString &globalVars,
                               const QString &function):
    _linked_plot_name(linkedPlot),
    _name(plotName),
    _global_vars(globalVars),
    _function(function),
    _last_updated_timestamp( - std::numeric_limits<double>::max() )
{

    QString qLinkedPlot = QString::fromStdString(_linked_plot_name);

    QString replaced_equation = _function;
    while(true)
    {
        int pos1=replaced_equation.indexOf("$$");
        if(pos1 == -1){
            break;
        }

        int pos2 = replaced_equation.indexOf("$$", pos1+2);
        if(pos2 == -1)
        {
            throw std::runtime_error("syntax error : invalid use of $$ macro");
        }

        QString channel_name = replaced_equation.mid(pos1+2, pos2-pos1-2);

        if(channel_name == qLinkedPlot)
        {
            // special case : user entered linkedPlot ; no need to add another channel
            replaced_equation.replace(QStringLiteral("$$%1$$").arg(channel_name), QStringLiteral("value"));
        }
        else
        {
            QString jsExpression = QString("CHANNEL_VALUES[%1]").arg(_used_channels.size());
            replaced_equation.replace(QStringLiteral("$$%1$$").arg(channel_name), jsExpression);
            _used_channels.push_back(channel_name.toStdString());
        }
    }
    _function_replaced = replaced_equation;

    //qDebug() << "final equation string : " << replaced_equation;
    initJsEngine();

    // TODO factoryse !!
    QJSValue calcFct = _jsEngine->evaluate("calc");

    if(calcFct.isError())
    {
        throw std::runtime_error("JS Engine : Error in calc function: " + calcFct.toString().toStdString());
    }

    QJSValue chan_values = _jsEngine->newArray(static_cast<quint32>(_used_channels.size()));

    for (unsigned int i=0;i<_used_channels.size();++i)
      chan_values.setProperty(static_cast<quint32>(i), QJSValue(1.0));

    QJSValue jsData = calcFct.call({QJSValue(0.0), QJSValue(1.0), chan_values});
    if (jsData.isError())
    {
        throw std::runtime_error("JS Engine : introspection output failled: " + jsData.toString().toStdString());
    }

    if (jsData.isNumber())
    {
      _plot_names.push_back(plotName);
    }
    else if(jsData.isArray())
    {
      const int length = jsData.property("length").toInt();
      for (int i=0;i<length;++i)
      {
        _plot_names.push_back(plotName+'.'+std::to_string(i));
      }
    }
    else if(jsData.isObject())
    {
      QJSValueIterator it(jsData);
      while (it.hasNext()) {
        it.next();
        _plot_names.push_back(plotName+'/'+ it.name().toStdString());
      }
    }
    qDebug() << "Register " << _plot_names.size() <<" outputs";
    for (const auto & name: _plot_names)
      qDebug() <<"name :"<< name.c_str();
}

void CustomFunction::calculateAndAdd(PlotDataMapRef &plotData)
{
    bool newly_added = false;
    std::vector<PlotData *> dst_data;
    for (auto && plot_name :_plot_names)
    {
      auto dst_data_it = plotData.numeric.find(plot_name);
      if(dst_data_it == plotData.numeric.end())
      {
        dst_data_it = plotData.addNumeric(plot_name);
        newly_added = true;
      }

      dst_data.push_back(&(dst_data_it->second));
      dst_data.back()->clear();
    }
    //_last_updated_timestamp = 0.0;

    try{
        calculate(plotData, dst_data);
    }
    catch(...)
    {
        if( newly_added )
        {
           for (auto && plot_name :_plot_names)
           {
             auto dst_data_it = plotData.numeric.find(plot_name);
             if (dst_data_it != plotData.numeric.end())
                plotData.numeric.erase( dst_data_it );
           }
        }
        std::rethrow_exception( std::current_exception() );
    }
}

void CustomFunction::initJsEngine()
{
    _jsEngine = std::unique_ptr<QJSEngine>( new QJSEngine() );

    QJSValue globalVarResult = _jsEngine->evaluate(_global_vars);
    if(globalVarResult.isError())
    {
        throw std::runtime_error("JS Engine : " + globalVarResult.toString().toStdString());
    }
    QString calcMethodStr = QString("function calc(time, value, CHANNEL_VALUES){with (Math){\n%1\n}}").arg(_function_replaced);
    _jsEngine->evaluate(calcMethodStr);
}

std::vector<PlotData::Point> CustomFunction::calculatePoint(QJSValue& calcFct,
                                const PlotData& src_data,
                                const std::vector<const PlotData*>& channels_data,
                                QJSValue& chan_values,
                                size_t point_index)
{
    const PlotData::Point &old_point = src_data.at(point_index);

    int chan_index = 0;
    for(const PlotData* chan_data: channels_data)
    {
        double value;
        int index = chan_data->getIndexFromX(old_point.x);
        if(index != -1){
            value = chan_data->at(index).y;
        }
        else{
            value = std::numeric_limits<double>::quiet_NaN();
        }
        chan_values.setProperty(static_cast<quint32>(chan_index++), QJSValue(value));
    }

    std::vector<PlotData::Point> new_points(_plot_names.size(), old_point);

    QJSValue jsData = calcFct.call({QJSValue(old_point.x), QJSValue(old_point.y), chan_values});
    if (jsData.isError())
    {
        throw std::runtime_error("JS Engine : " + jsData.toString().toStdString());
    }

    if (jsData.isNumber())
    {
      new_points[0].y = jsData.toNumber();
    }
    else if (jsData.isArray())
    {
      for (int i=0; i<new_points.size(); i++)
      {
          new_points[i].y = jsData.property(i).toNumber();
      }
    }
    else if (jsData.isObject())
    {
      QJSValueIterator it(jsData);
      unsigned int i=0;
      while (it.hasNext()) {
        it.next();
        new_points[i].y = it.value().toNumber();
        i++;
      }

    }

    return new_points;
}

void CustomFunction::update(PlotDataMapRef &plotData)
{
  std::vector<PlotData*> dst_data_array;

  for (const auto & plot_name: _plot_names)
  {
    auto it = plotData.numeric.find(plot_name);
    if(it == plotData.numeric.end())
    {
      it = plotData.addNumeric(plot_name);
    }

    dst_data_array.push_back(&(it->second));
  }

  calculate(plotData,  dst_data_array);
}


void CustomFunction::calculate(const PlotDataMapRef &plotData, std::vector<PlotData*>& dst_data_array)
{
    QJSValue calcFct = _jsEngine->evaluate("calc");

    if(calcFct.isError())
    {
        throw std::runtime_error("JS Engine : " + calcFct.toString().toStdString());
    }

    auto src_data_it = plotData.numeric.find(_linked_plot_name);
    if(src_data_it == plotData.numeric.end())
    {
        // failed! keep it empty
        return;
    }
    const PlotData& src_data = src_data_it->second;

    // clean up old data
    for (auto & dst_data : dst_data_array)
      dst_data->setMaximumRangeX( src_data.maximumRangeX() );

    std::vector<const PlotData*> channel_data;
    channel_data.reserve(_used_channels.size());

    for(const auto& channel: _used_channels)
    {
        auto it = plotData.numeric.find(channel);
        if(it == plotData.numeric.end())
        {
            throw std::runtime_error("Invalid channel name");
        }
        const PlotData* chan_data = &(it->second);
        channel_data.push_back(chan_data);
    }

    QJSValue chan_values = _jsEngine->newArray(static_cast<quint32>(_used_channels.size()));

    for(size_t i=0; i < src_data.size(); ++i)
    {
        if( src_data.at(i).x > _last_updated_timestamp)
        {
            const auto & calculated_points = calculatePoint(calcFct, src_data, channel_data, chan_values, i );
            for (size_t j=0;j < dst_data_array.size();++j)
              dst_data_array[j]->pushBack(calculated_points[j]);
        }
    }
    if (dst_data_array.size() !=0 
        && dst_data_array[0]->size() != 0)
    {
      _last_updated_timestamp = dst_data_array[0]->back().x;
    }
}

const std::string &CustomFunction::name() const
{
    return _name;
}

const std::vector<std::string> &CustomFunction::plot_names() const
{
    return _plot_names;
}

const std::string &CustomFunction::linkedPlotName() const
{
    return _linked_plot_name;
}

const QString &CustomFunction::globalVars() const
{
    return _global_vars;
}

const QString &CustomFunction::function() const
{
    return _function;
}



QDomElement CustomFunction::xmlSaveState(QDomDocument &doc) const
{
    QDomElement snippet = doc.createElement("snippet");
    snippet.setAttribute("name", QString::fromStdString(_name) );

    QDomElement linked = doc.createElement("linkedPlot");
    linked.appendChild( doc.createTextNode( QString::fromStdString(_linked_plot_name)) );
    snippet.appendChild(linked);

    QDomElement global = doc.createElement("global");
    global.appendChild( doc.createTextNode(_global_vars) );
    snippet.appendChild(global);

    QDomElement equation = doc.createElement("equation");
    equation.appendChild( doc.createTextNode(_function) );
    snippet.appendChild(equation);

    return snippet;
}

CustomPlotPtr CustomFunction::createFromXML(QDomElement &element)
{
    auto name   = element.attribute("name").toStdString();
    auto linkedPlot = element.firstChildElement("linkedPlot").text().trimmed().toStdString();
    auto globalVars = element.firstChildElement("global").text().trimmed();
    auto calcEquation = element.firstChildElement("equation").text().trimmed();

    return std::make_shared<CustomFunction>(linkedPlot, name, globalVars, calcEquation );
}

SnippetsMap GetSnippetsFromXML(const QString& xml_text)
{
    if( xml_text.isEmpty() ) return {};

    QDomDocument doc;
    QString parseErrorMsg;
    int parseErrorLine;
    if(!doc.setContent(xml_text, &parseErrorMsg, &parseErrorLine))
    {
        QMessageBox::critical(nullptr, "Error",
                              QString("Failed to parse snippets (xml), error %1 at line %2")
                              .arg(parseErrorMsg).arg(parseErrorLine));
        return {};
    }
    else
    {
        QDomElement snippets_element = doc.documentElement();
        return GetSnippetsFromXML(snippets_element);
    }
}

SnippetsMap GetSnippetsFromXML(const QDomElement &snippets_element)
{
    SnippetsMap snippets;

    for (auto elem = snippets_element.firstChildElement("snippet");
         !elem.isNull();
         elem = elem.nextSiblingElement("snippet"))
    {
        SnippetData snippet;
        snippet.name = elem.attribute("name");
        snippet.globalVars = elem.firstChildElement("global").text().trimmed();
        snippet.equation = elem.firstChildElement("equation").text().trimmed();
        snippets.insert( {snippet.name, snippet } );
    }
    return snippets;
}

QDomElement ExportSnippets(const SnippetsMap &snippets, QDomDocument &doc)
{
    auto snippets_root = doc.createElement("snippets");

    for (const auto& it: snippets )
    {
        const auto& snippet = it.second;

        auto element = doc.createElement("snippet");
        element.setAttribute("name", it.first);

        auto global_el = doc.createElement("global");
        global_el.appendChild( doc.createTextNode( snippet.globalVars ) );

        auto equation_el = doc.createElement("equation");
        equation_el.appendChild( doc.createTextNode( snippet.equation ) );

        element.appendChild( global_el );
        element.appendChild( equation_el );
        snippets_root.appendChild( element );
    }
    return snippets_root;
}

